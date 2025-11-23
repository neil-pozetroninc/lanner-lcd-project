#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <syslog.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#define PLCM_IOCTL_GET_KEYPAD   0x0C
#define STATE_FILE              "/var/run/lcd_cycle_state"
#define DAEMON_PIDFILE          "/run/lcd_button_daemon.pid"

#define BUTTON_LEFT  0xEF
#define BUTTON_RIGHT 0xE7

#define POLL_INTERVAL_MS 200
#define AUTO_CYCLE_SECONDS 5
#define MAX_IPS 10

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

        // Check if interface is up and not loopback
        if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK))
            continue;

        // Check for physical interfaces (skip virtual)
        if (strncmp(ifa->ifa_name, "lo", 2) == 0 ||
            strncmp(ifa->ifa_name, "docker", 6) == 0 ||
            strncmp(ifa->ifa_name, "veth", 4) == 0 ||
            strncmp(ifa->ifa_name, "br-", 3) == 0)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET) {
            count++;
        }
    }

    freeifaddrs(ifaddr);
    return count;
}

int get_total_states() {
    // Total states = 1 (model) + N (IPs) + 1 (hostname)
    int num_ips = count_ip_addresses();
    if (num_ips == 0) {
        num_ips = 1;  // Still show "No IP Address" state
    }
    return 1 + num_ips + 1;
}

int get_cycle_state() {
    FILE *f = fopen(STATE_FILE, "r");
    int state = 0;
    if (f) {
        fscanf(f, "%d", &state);
        fclose(f);
    }
    return state;
}

void set_cycle_state(int state, int total_states) {
    FILE *f = fopen(STATE_FILE, "w");
    if (f) {
        fprintf(f, "%d\n", state % total_states);
        fclose(f);
    }
}

void update_display() {
    system("/usr/local/bin/lcd_vitals");
}

int main() {
    int fd;
    int last_keypad = 0;
    int cycle_state;
    struct timespec sleep_time, remaining;
    time_t last_auto_cycle = time(NULL);
    time_t last_display = time(NULL);

    openlog("lcd_button_daemon", LOG_PID | LOG_NDELAY, LOG_DAEMON);

    syslog(LOG_INFO, "Starting LCD daemon...");

    if (daemon(0, 0) != 0) {
        syslog(LOG_ERR, "daemon() failed: %m");
        closelog();
        return 1;
    }

    FILE *pidfile = fopen(DAEMON_PIDFILE, "w");
    if (pidfile) {
        fprintf(pidfile, "%d\n", getpid());
        fclose(pidfile);
    }

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

    syslog(LOG_INFO, "Polling (200ms), auto-cycle (5s), display refresh (1s)");

    while (keep_running) {
        time_t now = time(NULL);
        int need_update = 0;
        int total_states = get_total_states();

        // Open device for button check
        fd = open("/dev/plcm_drv", O_RDWR);
        if (fd >= 0) {
            int current_keypad = ioctl(fd, PLCM_IOCTL_GET_KEYPAD, 0);

            // Button detection
            if ((current_keypad != last_keypad) && ((current_keypad & 0x40) != 0)) {
                cycle_state = get_cycle_state();

                if (current_keypad == BUTTON_LEFT) {
                    cycle_state = (cycle_state - 1 + total_states) % total_states;
                    set_cycle_state(cycle_state, total_states);
                    last_auto_cycle = now;
                    need_update = 1;
                    syslog(LOG_INFO, "LEFT button -> state %d/%d", cycle_state, total_states);
                } else if (current_keypad == BUTTON_RIGHT) {
                    cycle_state = (cycle_state + 1) % total_states;
                    set_cycle_state(cycle_state, total_states);
                    last_auto_cycle = now;
                    need_update = 1;
                    syslog(LOG_INFO, "RIGHT button -> state %d/%d", cycle_state, total_states);
                }
            }

            if ((current_keypad & 0x40) == 0) {
                last_keypad = current_keypad;
            }

            close(fd);
        }

        // Auto-cycle check
        if (!need_update && (now - last_auto_cycle) >= AUTO_CYCLE_SECONDS) {
            cycle_state = get_cycle_state();
            cycle_state = (cycle_state + 1) % total_states;
            set_cycle_state(cycle_state, total_states);
            last_auto_cycle = now;
            need_update = 1;
            syslog(LOG_DEBUG, "Auto-cycle -> state %d/%d", cycle_state, total_states);
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
