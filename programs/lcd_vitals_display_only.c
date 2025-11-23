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
#define STATE_FILE              "/var/run/lcd_cycle_state"
#define MAX_IPS                 10

typedef struct {
    char ifname[16];
    char ip[INET_ADDRSTRLEN];
} ip_info_t;

int collect_ip_addresses(ip_info_t *ips, int max_ips) {
    struct ifaddrs *ifaddr, *ifa;
    int count = 0;

    if (getifaddrs(&ifaddr) == -1) {
        return 0;
    }

    for (ifa = ifaddr; ifa != NULL && count < max_ips; ifa = ifa->ifa_next) {
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

            strncpy(ips[count].ifname, ifa->ifa_name, sizeof(ips[count].ifname) - 1);
            ips[count].ifname[sizeof(ips[count].ifname) - 1] = '\0';

            inet_ntop(AF_INET, &addr->sin_addr, ips[count].ip, sizeof(ips[count].ip));
            count++;
        }
    }

    freeifaddrs(ifaddr);
    return count;
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

    fd = open("/dev/plcm_drv", O_RDWR);
    if (fd < 0) {
        return 1;
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

    // Collect all IP addresses
    ip_info_t ips[MAX_IPS];
    int num_ips = collect_ip_addresses(ips, MAX_IPS);

    // Get cycle state and prepare line 2
    cycle_state = get_cycle_state();
    memset(line2, ' ', 40);

    if (cycle_state == 0) {
        // State 0: Show model name
        snprintf(line2, 21, "Lanner NCA-2510A");
    } else if (cycle_state >= 1 && cycle_state <= num_ips) {
        // State 1 to N: Show individual IP addresses
        int ip_index = cycle_state - 1;
        if (num_ips == 0) {
            snprintf(line2, 21, "No IP Address");
        } else {
            snprintf(line2, 21, "%s:%s", ips[ip_index].ifname, ips[ip_index].ip);
        }
    } else {
        // State N+1: Show hostname
        get_hostname(temp_buf, sizeof(temp_buf));
        snprintf(line2, 21, "Host: %s", temp_buf);
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

    // Display only - do not modify cycle state
    // (Button daemon handles all cycling)

    close(fd);
    return 0;
}
