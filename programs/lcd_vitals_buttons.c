#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#define PLCM_IOCTL_BACKLIGHT    0x01
#define PLCM_IOCTL_CLEARDISPLAY 0x03
#define PLCM_IOCTL_DISPLAY_D    0x07
#define PLCM_IOCTL_DISPLAY_C    0x08
#define PLCM_IOCTL_DISPLAY_B    0x09
#define PLCM_IOCTL_SET_LINE     0x0D
#define PLCM_IOCTL_GET_KEYPAD   0x0C
#define STATE_FILE              "/var/run/lcd_cycle_state"
#define KEYPAD_FILE             "/var/run/lcd_last_keypad"

#define BUTTON_LEFT  0xEF
#define BUTTON_RIGHT 0xE7

void get_ip_addresses(char *buf, size_t buflen) {
    struct ifaddrs *ifaddr, *ifa;
    int found = 0;

    buf[0] = '\0';

    if (getifaddrs(&ifaddr) == -1) {
        snprintf(buf, buflen, "No IP");
        return;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
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
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));

            if (found == 0) {
                snprintf(buf, buflen, "%s:%s", ifa->ifa_name, ip);
                found = 1;
            } else {
                // Multiple IPs - show count
                snprintf(buf, buflen, "Multiple IPs");
                break;
            }
        }
    }

    if (!found) {
        snprintf(buf, buflen, "No IP Address");
    }

    freeifaddrs(ifaddr);
}

void get_hostname(char *buf, size_t buflen) {
    if (gethostname(buf, buflen) != 0) {
        snprintf(buf, buflen, "Unknown Host");
    }
    buf[buflen - 1] = '\0';  // Ensure null termination
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

void set_cycle_state(int state) {
    FILE *f = fopen(STATE_FILE, "w");
    if (f) {
        fprintf(f, "%d\n", state);
        fclose(f);
    }
}

int get_last_keypad() {
    FILE *f = fopen(KEYPAD_FILE, "r");
    int value = 0;

    if (f) {
        fscanf(f, "%x", &value);
        fclose(f);
    }

    return value;
}

void set_last_keypad(int value) {
    FILE *f = fopen(KEYPAD_FILE, "w");
    if (f) {
        fprintf(f, "0x%02X\n", value);
        fclose(f);
    }
}

int main() {
    int fd;
    char line1[41];
    char line2[41];
    FILE *fp;
    char buffer[256];
    float load1;
    unsigned long mem_total = 0, mem_available = 0;
    int mem_used_pct = 0;
    time_t now;
    struct tm *tm_info;
    char time_str[20];
    char temp_buf[128];
    int cycle_state;
    int last_keypad, current_keypad;
    int button_pressed = 0;

    fd = open("/dev/plcm_drv", O_RDWR);
    if (fd < 0) {
        return 1;
    }

    // Get current cycle state and last keypad value
    cycle_state = get_cycle_state();
    last_keypad = get_last_keypad();

    // Read current keypad state
    current_keypad = ioctl(fd, PLCM_IOCTL_GET_KEYPAD, 0);

    // Detect button press: value changed AND bit 0x40 is SET
    if ((current_keypad != last_keypad) && ((current_keypad & 0x40) != 0)) {
        // Button was pressed
        if (current_keypad == BUTTON_LEFT) {
            // LEFT button: cycle backward
            cycle_state = (cycle_state - 1 + 3) % 3;
            button_pressed = 1;
        } else if (current_keypad == BUTTON_RIGHT) {
            // RIGHT button: cycle forward
            cycle_state = (cycle_state + 1) % 3;
            button_pressed = 1;
        }
    }

    // If no button was pressed, auto-cycle forward
    if (!button_pressed) {
        cycle_state = (cycle_state + 1) % 3;
    }

    // Save cycle state for next run
    set_cycle_state(cycle_state);

    // Only save keypad state if NOT currently pressing a button
    // This prevents saving transient button-press values
    if ((current_keypad & 0x40) == 0) {
        set_last_keypad(current_keypad);
    }

    // Ensure display is configured
    ioctl(fd, PLCM_IOCTL_BACKLIGHT, 1);
    ioctl(fd, PLCM_IOCTL_DISPLAY_D, 1);
    ioctl(fd, PLCM_IOCTL_DISPLAY_C, 0);
    ioctl(fd, PLCM_IOCTL_DISPLAY_B, 0);

    // Get load average
    fp = fopen("/proc/loadavg", "r");
    if (fp) {
        if (fscanf(fp, "%f", &load1) != 1) {
            load1 = 0.0;
        }
        fclose(fp);
    }

    // Get memory info
    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp)) {
            if (sscanf(buffer, "MemTotal: %lu kB", &mem_total) == 1) continue;
            if (sscanf(buffer, "MemAvailable: %lu kB", &mem_available) == 1) break;
        }
        fclose(fp);
    }

    if (mem_total > 0 && mem_available > 0) {
        mem_used_pct = 100 - (mem_available * 100 / mem_total);
    }

    // Get current time
    time(&now);
    tm_info = localtime(&now);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

    // Format line 1
    memset(line1, ' ', 40);
    snprintf(line1, 21, "L:%.2f M:%d%% %s", load1, mem_used_pct, time_str);
    for (int i = 0; i < 40; i++) {
        if (line1[i] == '\0') line1[i] = ' ';
    }
    line1[40] = '\0';

    // Prepare line 2 based on current cycle state
    memset(line2, ' ', 40);

    switch (cycle_state) {
        case 0:
            // Show model name
            snprintf(line2, 21, "Lanner NCA-2510A");
            break;
        case 1:
            // Show IP address
            get_ip_addresses(temp_buf, sizeof(temp_buf));
            snprintf(line2, 21, "%s", temp_buf);
            break;
        case 2:
            // Show hostname
            get_hostname(temp_buf, sizeof(temp_buf));
            snprintf(line2, 21, "Host: %s", temp_buf);
            break;
    }

    // Replace null terminators with spaces
    for (int i = 0; i < 40; i++) {
        if (line2[i] == '\0') line2[i] = ' ';
    }
    line2[40] = '\0';

    // Write to LCD
    ioctl(fd, PLCM_IOCTL_SET_LINE, 1);
    write(fd, line1, 40);

    ioctl(fd, PLCM_IOCTL_SET_LINE, 2);
    write(fd, line2, 40);

    close(fd);
    return 0;
}
