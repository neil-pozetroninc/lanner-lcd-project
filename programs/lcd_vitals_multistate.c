#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <time.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/statvfs.h>
#include <glob.h>
#include <dirent.h>

#define PLCM_IOCTL_BACKLIGHT    0x01
#define PLCM_IOCTL_CLEARDISPLAY 0x03
#define PLCM_IOCTL_DISPLAY_D    0x07
#define PLCM_IOCTL_DISPLAY_C    0x08
#define PLCM_IOCTL_DISPLAY_B    0x09
#define PLCM_IOCTL_SET_LINE     0x0D
#define STATE_FILE_LINE1        "/var/run/lcd_line1_state"
#define STATE_FILE_LINE2        "/var/run/lcd_cycle_state"
#define MAX_IPS                 10

typedef struct {
    char ifname[16];
    char ip[INET_ADDRSTRLEN];
} ip_info_t;

// Known virtual interface prefixes - used as fallback when sysfs unavailable
static const char *virtual_prefixes[] = {
    // Basic
    "lo",       // Loopback
    "dummy",    // Dummy interface (testing/debugging)
    // Containers
    "docker",   // Docker bridge
    "veth",     // Virtual Ethernet Pair (container networking)
    "cali",     // Calico (Kubernetes)
    "flannel",  // Flannel (Kubernetes)
    "cni",      // Container Network Interface bridge
    // VMs / Hypervisors
    "virbr",    // Virtual Bridge (libvirt/KVM)
    "vnet",     // Virtual Network (KVM/QEMU guest tap)
    "vbox",     // VirtualBox
    // VPN / Tunnels
    "tun",      // Layer 3 tunnel (OpenVPN, Tailscale)
    "tap",      // Layer 2 tunnel (VMs, OpenVPN)
    "wg",       // WireGuard VPN
    "ppp",      // Point-to-Point Protocol (VPNs, DSL)
    // Overlays
    "vxlan",    // Virtual Extensible LAN
    "geneve",   // Generic Network Virtualization Encapsulation
    "gre",      // Generic Routing Encapsulation (also matches gretap)
    "ipip",     // IP-in-IP tunneling
    "tunl",     // IP-in-IP tunneling
    // L2 Logical
    "br",       // Ethernet Bridge (br0, br-, bridge)
    "bond",     // Bonding (Link Aggregation)
    "team",     // Network Teaming
    "macvlan",  // Virtual MAC on physical interface
    "ipvlan",   // Virtual IP on physical interface
    NULL
};

// Check if interface name matches known virtual prefixes
static int matches_virtual_prefix(const char *ifname) {
    for (int i = 0; virtual_prefixes[i] != NULL; i++) {
        size_t prefix_len = strlen(virtual_prefixes[i]);
        if (strncmp(ifname, virtual_prefixes[i], prefix_len) == 0) {
            return 1;
        }
    }
    return 0;
}

// Returns 1 if the interface is virtual, 0 if physical
// Uses readlink to check if device symlink points to /sys/devices/virtual/
// Falls back to prefix matching when sysfs is unavailable
int is_virtual_interface(const char *ifname) {
    char path[256];
    char target[4096];
    ssize_t len;
    int written;

    // Input validation: NULL or empty name
    if (ifname == NULL || ifname[0] == '\0') {
        return 1;  // Invalid input, treat as virtual (skip it)
    }

    // Validate interface name length (IFNAMSIZ is typically 16 including null)
    size_t name_len = strlen(ifname);
    if (name_len >= IFNAMSIZ) {
        return 1;  // Too long to be valid, treat as virtual
    }

    // Check for path traversal characters - interface names cannot contain /
    if (strchr(ifname, '/') != NULL) {
        return 1;  // Invalid character, treat as virtual
    }

    // Exact match for loopback (optimization - it has no device symlink anyway)
    if (strcmp(ifname, "lo") == 0) {
        return 1;
    }

    // Build path to device symlink with truncation check
    written = snprintf(path, sizeof(path), "/sys/class/net/%s/device", ifname);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return 1;  // Truncation or error, treat as virtual
    }

    // Read the symlink target to determine if physical or virtual
    len = readlink(path, target, sizeof(target) - 1);
    if (len < 0) {
        // ENOENT = no device symlink = definitely virtual (like loopback)
        if (errno == ENOENT) {
            return 1;
        }
        // Other errors (EACCES, etc.) = sysfs unavailable (containers/chroots)
        // Fall back to prefix-based detection using known virtual interface names
        return matches_virtual_prefix(ifname);
    }

    // Null-terminate the symlink target
    target[len] = '\0';

    // Check if the symlink target points to the virtual subsystem
    // Virtual devices have symlinks like: ../../devices/virtual/net/...
    // Physical devices point to: ../../devices/pci0000:00/...
    if (strstr(target, "/virtual/") != NULL) {
        return 1;  // Virtual interface
    }

    return 0;  // Physical interface
}

int collect_ip_addresses(ip_info_t *ips, int max_ips) {
    struct ifaddrs *ifaddr, *ifa;
    int count = 0;

    if (getifaddrs(&ifaddr) == -1) {
        return 0;
    }

    for (ifa = ifaddr; ifa != NULL && count < max_ips; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK))
            continue;

        // Skip virtual interfaces - only show physical NICs
        if (is_virtual_interface(ifa->ifa_name))
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
    buf[buflen - 1] = '\0';
}

int get_cpu_temp() {
    // Try multiple thermal zone paths, skip zones with invalid readings
    const char *thermal_paths[] = {
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/thermal/thermal_zone1/temp",
        "/sys/class/thermal/thermal_zone2/temp",
        NULL
    };

    for (int i = 0; thermal_paths[i]; i++) {
        FILE *fp = fopen(thermal_paths[i], "r");
        if (fp) {
            int temp_millidegrees;
            if (fscanf(fp, "%d", &temp_millidegrees) == 1) {
                fclose(fp);
                int temp_celsius = temp_millidegrees / 1000;
                // Skip invalid readings (0 or negative usually means disabled/broken sensor)
                if (temp_celsius > 0 && temp_celsius < 150) {
                    return temp_celsius;
                }
            } else {
                fclose(fp);
            }
        }
    }
    return -1;  // Not available
}

int get_disk_usage() {
    struct statvfs stat;
    if (statvfs("/", &stat) != 0) {
        return -1;
    }
    unsigned long total = stat.f_blocks * stat.f_frsize;
    unsigned long avail = stat.f_bavail * stat.f_frsize;
    unsigned long used = total - avail;
    return (int)((used * 100) / total);
}

void get_uptime_str(char *buf, size_t buflen) {
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp) {
        snprintf(buf, buflen, "?");
        return;
    }

    double uptime_seconds;
    if (fscanf(fp, "%lf", &uptime_seconds) != 1) {
        fclose(fp);
        snprintf(buf, buflen, "?");
        return;
    }
    fclose(fp);

    int days = (int)(uptime_seconds / 86400);
    int hours = (int)((uptime_seconds - days * 86400) / 3600);
    int mins = (int)((uptime_seconds - days * 86400 - hours * 3600) / 60);

    if (days > 0) {
        snprintf(buf, buflen, "%dd%dh", days, hours);
    } else if (hours > 0) {
        snprintf(buf, buflen, "%dh%dm", hours, mins);
    } else {
        snprintf(buf, buflen, "%dm", mins);
    }
}

int get_process_count() {
    DIR *dir = opendir("/proc");
    if (!dir) return -1;

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            // Check if directory name is numeric (PID)
            char *endptr;
            strtol(entry->d_name, &endptr, 10);
            if (*endptr == '\0') {
                count++;
            }
        }
    }
    closedir(dir);
    return count;
}

int get_swap_usage() {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return -1;

    char buffer[256];
    unsigned long swap_total = 0, swap_free = 0;

    while (fgets(buffer, sizeof(buffer), fp)) {
        if (sscanf(buffer, "SwapTotal: %lu kB", &swap_total) == 1) continue;
        if (sscanf(buffer, "SwapFree: %lu kB", &swap_free) == 1) break;
    }
    fclose(fp);

    if (swap_total == 0) return -1;  // No swap configured
    return 100 - (swap_free * 100 / swap_total);
}

#define NET_STATS_FILE "/var/run/lcd_net_stats"

typedef struct {
    unsigned long rx_bytes;
    unsigned long tx_bytes;
    time_t timestamp;
} net_stats_t;

void get_network_rates(char *buf, size_t buflen) {
    static char active_if_name[16] = "";
    const char *active_if = NULL;

    // Dynamically find first active physical interface
    DIR *net_dir = opendir("/sys/class/net");
    if (!net_dir) {
        snprintf(buf, buflen, "No Network");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(net_dir)) != NULL) {
        // Skip special entries and virtual interfaces
        if (entry->d_name[0] == '.' ||
            strncmp(entry->d_name, "lo", 2) == 0 ||
            strncmp(entry->d_name, "docker", 6) == 0 ||
            strncmp(entry->d_name, "veth", 4) == 0 ||
            strncmp(entry->d_name, "br-", 3) == 0) {
            continue;
        }

        // Check if interface is up
        char operstate_path[256];
        snprintf(operstate_path, sizeof(operstate_path), "/sys/class/net/%s/operstate", entry->d_name);
        FILE *fp = fopen(operstate_path, "r");
        if (fp) {
            char state[16];
            if (fgets(state, sizeof(state), fp)) {
                if (strncmp(state, "up", 2) == 0) {
                    strncpy(active_if_name, entry->d_name, sizeof(active_if_name) - 1);
                    active_if_name[sizeof(active_if_name) - 1] = '\0';
                    active_if = active_if_name;
                    fclose(fp);
                    break;
                }
            }
            fclose(fp);
        }
    }
    closedir(net_dir);

    if (!active_if) {
        snprintf(buf, buflen, "No Network");
        return;
    }

    char rx_path[256], tx_path[256];
    snprintf(rx_path, sizeof(rx_path), "/sys/class/net/%s/statistics/rx_bytes", active_if);
    snprintf(tx_path, sizeof(tx_path), "/sys/class/net/%s/statistics/tx_bytes", active_if);

    FILE *rx_fp = fopen(rx_path, "r");
    FILE *tx_fp = fopen(tx_path, "r");

    if (!rx_fp || !tx_fp) {
        if (rx_fp) fclose(rx_fp);
        if (tx_fp) fclose(tx_fp);
        snprintf(buf, buflen, "Stats N/A");
        return;
    }

    unsigned long current_rx = 0, current_tx = 0;
    if (fscanf(rx_fp, "%lu", &current_rx) != 1 || fscanf(tx_fp, "%lu", &current_tx) != 1) {
        fclose(rx_fp);
        fclose(tx_fp);
        snprintf(buf, buflen, "Stats N/A");
        return;
    }
    fclose(rx_fp);
    fclose(tx_fp);

    time_t now = time(NULL);
    net_stats_t prev = {0, 0, 0};
    FILE *stats_fp = fopen(NET_STATS_FILE, "r");
    int first_run = 0;

    if (stats_fp) {
        fscanf(stats_fp, "%lu %lu %ld", &prev.rx_bytes, &prev.tx_bytes, &prev.timestamp);
        fclose(stats_fp);
    } else {
        first_run = 1;  // No previous stats - first run or stats file deleted
    }

    // Save current stats for next run
    stats_fp = fopen(NET_STATS_FILE, "w");
    if (stats_fp) {
        fprintf(stats_fp, "%lu %lu %ld\n", current_rx, current_tx, now);
        fclose(stats_fp);
    }

    // On first run, display zero rates (baseline saved for next iteration)
    if (first_run) {
        snprintf(buf, buflen, "RX:0B TX:0B");
        return;
    }

    double time_delta = difftime(now, prev.timestamp);
    if (time_delta < 0.1) time_delta = 1.0;

    // Detect counter wrap or NIC reset (current < previous)
    if (current_rx < prev.rx_bytes || current_tx < prev.tx_bytes) {
        snprintf(buf, buflen, "Net: Reset");
        return;
    }

    long rx_rate = (long)((current_rx - prev.rx_bytes) / time_delta);
    long tx_rate = (long)((current_tx - prev.tx_bytes) / time_delta);

    char rx_str[10], tx_str[10];
    if (rx_rate < 1024) {
        snprintf(rx_str, sizeof(rx_str), "%ldB", rx_rate);
    } else if (rx_rate < 1024*1024) {
        snprintf(rx_str, sizeof(rx_str), "%ldK", rx_rate/1024);
    } else {
        snprintf(rx_str, sizeof(rx_str), "%.1fM", rx_rate/(1024.0*1024.0));
    }

    if (tx_rate < 1024) {
        snprintf(tx_str, sizeof(tx_str), "%ldB", tx_rate);
    } else if (tx_rate < 1024*1024) {
        snprintf(tx_str, sizeof(tx_str), "%ldK", tx_rate/1024);
    } else {
        snprintf(tx_str, sizeof(tx_str), "%.1fM", tx_rate/(1024.0*1024.0));
    }

    snprintf(buf, buflen, "RX:%s TX:%s", rx_str, tx_str);
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

int main() {
    int fd;
    char line1[41];
    char line2[41];
    FILE *fp;
    char buffer[256];
    float load1 = -1.0f;  // -1 indicates unavailable
    unsigned long mem_total = 0, mem_available = 0;
    int mem_used_pct = -1;  // -1 indicates unknown/parse failure
    time_t now;
    struct tm *tm_info;
    char time_str[20];
    char temp_buf[128];
    int line1_state, line2_state;

    fd = open("/dev/plcm_drv", O_RDWR);
    if (fd < 0) {
        return 1;
    }

    // Ensure display is configured
    ioctl(fd, PLCM_IOCTL_BACKLIGHT, 1);
    ioctl(fd, PLCM_IOCTL_DISPLAY_D, 1);
    ioctl(fd, PLCM_IOCTL_DISPLAY_C, 0);
    ioctl(fd, PLCM_IOCTL_DISPLAY_B, 0);

    // Get system stats for line 1
    fp = fopen("/proc/loadavg", "r");
    if (fp) {
        fscanf(fp, "%f", &load1);
        fclose(fp);
    }

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

    time(&now);
    tm_info = localtime(&now);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

    // Format LINE 1 based on state
    line1_state = get_state(STATE_FILE_LINE1);
    memset(line1, ' ', 40);

    switch (line1_state) {
        case 0:  // Load/Mem/Time
            if (load1 >= 0 && mem_used_pct >= 0) {
                snprintf(line1, 21, "L:%.2f M:%d%% %s", load1, mem_used_pct, time_str);
            } else if (load1 >= 0 && mem_used_pct < 0) {
                snprintf(line1, 21, "L:%.2f M:N/A %s", load1, time_str);
            } else if (load1 < 0 && mem_used_pct >= 0) {
                snprintf(line1, 21, "L:N/A M:%d%% %s", mem_used_pct, time_str);
            } else {
                snprintf(line1, 21, "L:N/A M:N/A %s", time_str);
            }
            break;
        case 1: {  // CPU temp/Disk/Uptime
            int cpu_temp = get_cpu_temp();
            int disk_pct = get_disk_usage();
            get_uptime_str(temp_buf, sizeof(temp_buf));
            if (cpu_temp >= 0 && disk_pct >= 0) {
                snprintf(line1, 21, "CPU:%dC D:%d%% /%s", cpu_temp, disk_pct, temp_buf);
            } else if (cpu_temp >= 0) {
                snprintf(line1, 21, "CPU:%dC Up:%s", cpu_temp, temp_buf);
            } else if (disk_pct >= 0) {
                snprintf(line1, 21, "Disk:%d%% Up:%s", disk_pct, temp_buf);
            } else {
                snprintf(line1, 21, "Uptime: %s", temp_buf);
            }
            break;
        }
        case 2: {  // Network RX/TX
            get_network_rates(temp_buf, sizeof(temp_buf));
            snprintf(line1, 21, "%s", temp_buf);
            break;
        }
        case 3: {  // Uptime/Processes/Swap
            get_uptime_str(temp_buf, sizeof(temp_buf));
            int procs = get_process_count();
            int swap_pct = get_swap_usage();

            if (procs >= 0 && swap_pct >= 0) {
                snprintf(line1, 21, "Up:%s P:%d S:%d%%", temp_buf, procs, swap_pct);
            } else if (procs >= 0) {
                snprintf(line1, 21, "Up:%s P:%d", temp_buf, procs);
            } else if (swap_pct >= 0) {
                snprintf(line1, 21, "Up:%s S:%d%%", temp_buf, swap_pct);
            } else {
                snprintf(line1, 21, "Up:%s", temp_buf);
            }
            break;
        }
    }

    for (int i = 0; i < 40; i++) {
        if (line1[i] == '\0') line1[i] = ' ';
    }
    line1[40] = '\0';

    // Format LINE 2 based on state
    line2_state = get_state(STATE_FILE_LINE2);
    ip_info_t ips[MAX_IPS];
    int num_ips = collect_ip_addresses(ips, MAX_IPS);

    memset(line2, ' ', 40);

    if (line2_state == 0) {
        // State 0: Always show model name
        snprintf(line2, 21, "Lanner NCA-2510A");
    } else if (line2_state >= 1 && line2_state <= num_ips) {
        // States 1 to num_ips: Show IP addresses (only when num_ips > 0)
        int ip_index = line2_state - 1;
        snprintf(line2, 21, "%s:%s", ips[ip_index].ifname, ips[ip_index].ip);
    } else {
        // Last state: Always show hostname
        // When num_ips=0: daemon has 2 states (0=model, 1=hostname)
        // When num_ips>0: daemon has 2+num_ips states (0=model, 1..num_ips=IPs, num_ips+1=hostname)
        get_hostname(temp_buf, sizeof(temp_buf));
        snprintf(line2, 21, "Host: %s", temp_buf);
    }

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
