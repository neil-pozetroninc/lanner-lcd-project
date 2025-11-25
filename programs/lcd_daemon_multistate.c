#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <syslog.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include "network_interface_utils.h"

#define PLCM_IOCTL_GET_KEYPAD   0x0C
#define STATE_FILE_LINE1        "/var/run/lcd_line1_state"
#define STATE_FILE_LINE2        "/var/run/lcd_cycle_state"
#define DAEMON_PIDFILE          "/run/lcd_button_daemon.pid"

#define BUTTON_LEFT  0xEF
#define BUTTON_RIGHT 0xE7
#define BUTTON_UP    0xC7
#define BUTTON_DOWN  0xCF

#define POLL_INTERVAL_MS 200
#define AUTO_CYCLE_LINE1_SECONDS 10
#define AUTO_CYCLE_LINE2_SECONDS 5
#define INTERFACE_CHECK_INTERVAL_SECONDS 30

#define MAX_IPS 10
#define LINE1_STATES 4

static volatile sig_atomic_t keep_running = 1;

void signal_handler(int signum) {
    keep_running = 0;
}

int count_ip_addresses() {
    struct ifaddrs *ifaddr, *ifa;
    int count = 0;

    if (getifaddrs(&ifaddr) == -1) {
        return 0;
    }

    for (ifa = ifaddr; ifa != NULL && count < MAX_IPS; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK))
            continue;

        // Skip virtual interfaces - only count physical NICs
        if (is_virtual_interface(ifa->ifa_name))
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET) {
            count++;
        }
    }

    freeifaddrs(ifaddr);
    return count;
}

int get_line2_total_states() {
    int num_ips = count_ip_addresses();
    // Line 2 states: Model + num_ips + Hostname = 1 + num_ips + 1
    return 1 + num_ips + 1;
}

int get_state(const char *file) {
    FILE *f = fopen(file, "r");
    int state = 0;
    if (f) {
        fscanf(f, "%d", &state);
        fclose(f);
    }
    return state;
}

void set_state(const char *file, int state, int total_states) {
    FILE *f = fopen(file, "w");
    if (f) {
        fprintf(f, "%d\n", state % total_states);
        fclose(f);
    }
}

void update_display() {
    // Call the multistate display renderer
    int ret = system("/usr/local/bin/lcd_vitals");

    if (ret == -1) {
        // system() call itself failed (fork/exec error)
        syslog(LOG_ERR, "Failed to execute lcd_vitals: system() error");
    } else if (WIFEXITED(ret)) {
        // Process exited normally, check exit code
        int exit_code = WEXITSTATUS(ret);
        if (exit_code != 0) {
            syslog(LOG_WARNING, "lcd_vitals exited with code %d", exit_code);
        }
    } else if (WIFSIGNALED(ret)) {
        // Process killed by signal
        syslog(LOG_WARNING, "lcd_vitals killed by signal %d", WTERMSIG(ret));
    }
}

int main() {
    int fd;
    int last_keypad = 0;
    int line1_state, line2_state;
    struct timespec sleep_time, remaining;
    time_t last_auto_cycle_line1 = time(NULL);
    time_t last_auto_cycle_line2 = time(NULL);
    time_t last_display = time(NULL);
    time_t last_interface_check = 0;  // Force initial check
    int cached_line2_total_states = 0;

    openlog("lcd_button_daemon", LOG_PID | LOG_NDELAY, LOG_DAEMON);

    syslog(LOG_INFO, "Starting LCD daemon (multistate)...");

    if (daemon(0, 0) != 0) {
        syslog(LOG_ERR, "daemon() failed: %m");
        closelog();
        return 1;
    }

    // Create and lock PID file to prevent multiple instances
    int pidfile_fd = open(DAEMON_PIDFILE, O_CREAT | O_RDWR, 0644);
    if (pidfile_fd < 0) {
        syslog(LOG_ERR, "Failed to create PID file: %m");
        return 1;
    }

    if (flock(pidfile_fd, LOCK_EX | LOCK_NB) < 0) {
        syslog(LOG_ERR, "Another instance is already running");
        close(pidfile_fd);
        return 1;
    }

    // Write PID to file
    if (ftruncate(pidfile_fd, 0) == 0) {
        char pid_str[32];
        snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
        if (write(pidfile_fd, pid_str, strlen(pid_str)) < 0) {
            syslog(LOG_WARNING, "Failed to write PID: %m");
        }
    }
    // Keep fd open to maintain lock

    syslog(LOG_INFO, "LCD daemon running (PID: %d)", getpid());

    nice(5);
    struct rlimit rlim = {64, 64};
    setrlimit(RLIMIT_NOFILE, &rlim);

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    memset(&sleep_time, 0, sizeof(sleep_time));
    sleep_time.tv_sec = 0;
    sleep_time.tv_nsec = POLL_INTERVAL_MS * 1000000L;

    // Initial display
    update_display();

    syslog(LOG_INFO, "Polling (200ms), line1-cycle (10s), line2-cycle (5s), refresh (1s)");

    while (keep_running) {
        time_t now = time(NULL);
        int need_update = 0;

        // Cache interface count (only recompute every 30 seconds)
        if (now - last_interface_check >= INTERFACE_CHECK_INTERVAL_SECONDS) {
            cached_line2_total_states = get_line2_total_states();
            last_interface_check = now;
        }
        int line2_total_states = cached_line2_total_states;

        // Open device for button check
        fd = open("/dev/plcm_drv", O_RDWR);
        if (fd >= 0) {
            int current_keypad = ioctl(fd, PLCM_IOCTL_GET_KEYPAD, 0);

            // Button detection
            if ((current_keypad != last_keypad) && ((current_keypad & 0x40) != 0)) {
                if (current_keypad == BUTTON_UP) {
                    line1_state = get_state(STATE_FILE_LINE1);
                    line1_state = (line1_state - 1 + LINE1_STATES) % LINE1_STATES;
                    set_state(STATE_FILE_LINE1, line1_state, LINE1_STATES);
                    last_auto_cycle_line1 = now;
                    need_update = 1;
                    syslog(LOG_INFO, "UP button -> line1 state %d/%d", line1_state, LINE1_STATES);
                } else if (current_keypad == BUTTON_DOWN) {
                    line1_state = get_state(STATE_FILE_LINE1);
                    line1_state = (line1_state + 1) % LINE1_STATES;
                    set_state(STATE_FILE_LINE1, line1_state, LINE1_STATES);
                    last_auto_cycle_line1 = now;
                    need_update = 1;
                    syslog(LOG_INFO, "DOWN button -> line1 state %d/%d", line1_state, LINE1_STATES);
                } else if (current_keypad == BUTTON_LEFT) {
                    line2_state = get_state(STATE_FILE_LINE2);
                    line2_state = (line2_state - 1 + line2_total_states) % line2_total_states;
                    set_state(STATE_FILE_LINE2, line2_state, line2_total_states);
                    last_auto_cycle_line2 = now;
                    need_update = 1;
                    syslog(LOG_INFO, "LEFT button -> line2 state %d/%d", line2_state, line2_total_states);
                } else if (current_keypad == BUTTON_RIGHT) {
                    line2_state = get_state(STATE_FILE_LINE2);
                    line2_state = (line2_state + 1) % line2_total_states;
                    set_state(STATE_FILE_LINE2, line2_state, line2_total_states);
                    last_auto_cycle_line2 = now;
                    need_update = 1;
                    syslog(LOG_INFO, "RIGHT button -> line2 state %d/%d", line2_state, line2_total_states);
                }
            }

            if ((current_keypad & 0x40) == 0) {
                last_keypad = current_keypad;
            }

            close(fd);
        }

        // Auto-cycle line 1 (every 10 seconds)
        if (!need_update && (now - last_auto_cycle_line1) >= AUTO_CYCLE_LINE1_SECONDS) {
            line1_state = get_state(STATE_FILE_LINE1);
            line1_state = (line1_state + 1) % LINE1_STATES;
            set_state(STATE_FILE_LINE1, line1_state, LINE1_STATES);
            last_auto_cycle_line1 = now;
            need_update = 1;
            syslog(LOG_DEBUG, "Auto-cycle line1 -> state %d/%d", line1_state, LINE1_STATES);
        }

        // Auto-cycle line 2 (every 5 seconds)
        if (!need_update && (now - last_auto_cycle_line2) >= AUTO_CYCLE_LINE2_SECONDS) {
            line2_state = get_state(STATE_FILE_LINE2);
            line2_state = (line2_state + 1) % line2_total_states;
            set_state(STATE_FILE_LINE2, line2_state, line2_total_states);
            last_auto_cycle_line2 = now;
            need_update = 1;
            syslog(LOG_DEBUG, "Auto-cycle line2 -> state %d/%d", line2_state, line2_total_states);
        }

        // Display refresh (every 1 second for system stats)
        if (!need_update && (now - last_display) >= 1) {
            need_update = 1;
        }

        if (need_update) {
            update_display();
            last_display = now;
        }

        while (clock_nanosleep(CLOCK_MONOTONIC, 0, &sleep_time, &remaining) != 0) {
            sleep_time = remaining;
        }
        sleep_time.tv_sec = 0;
        sleep_time.tv_nsec = POLL_INTERVAL_MS * 1000000L;
    }

    unlink(DAEMON_PIDFILE);
    syslog(LOG_INFO, "LCD daemon stopped");
    closelog();

    return 0;
}
