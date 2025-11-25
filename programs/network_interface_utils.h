#ifndef NETWORK_INTERFACE_UTILS_H
#define NETWORK_INTERFACE_UTILS_H

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <net/if.h>

// Known virtual interface prefixes - used as fallback when sysfs unavailable
// NOTE: Excludes bond/bridge/team - these are "logical" and should be shown
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
    "incusbr",  // Incus container bridge
    "lxdbr",    // LXD container bridge
    // VPN / Tunnels
    "tun",      // Layer 3 tunnel (OpenVPN)
    "tap",      // Layer 2 tunnel (VMs, OpenVPN)
    "wg",       // WireGuard VPN
    "ppp",      // Point-to-Point Protocol (VPNs, DSL)
    "tailscale", // Tailscale VPN
    "zt",       // ZeroTier
    // Overlays / Tunnels
    "vxlan",    // Virtual Extensible LAN
    "geneve",   // Generic Network Virtualization Encapsulation
    "gre",      // Generic Routing Encapsulation (also matches gretap)
    "ipip",     // IP-in-IP tunneling
    "tunl",     // IP-in-IP tunneling
    "sit",      // Simple Internet Transition (IPv6-in-IPv4)
    "ip6gre",   // IPv6 GRE tunnel
    "ip6tnl",   // IPv6 tunnel
    "erspan",   // Encapsulated Remote SPAN
    "gtp",      // GPRS Tunneling Protocol
    "ifb",      // Intermediate Functional Block (traffic shaping)
    NULL
};

// Logical interfaces that should be shown (even though they're in /sys/devices/virtual/)
// These carry real IPs and are commonly used for management on appliances
// NOTE: macvlan/ipvlan are excluded - they're typically for containers, not management
static const char *logical_interface_prefixes[] = {
    "bond",     // Bonding (Link Aggregation)
    "br",       // Ethernet Bridge
    "team",     // Network Teaming
    NULL
};

// Check if interface name matches known virtual prefixes
// Returns 1 if it matches, 0 otherwise
// Uses delimiter checking to avoid false positives (e.g., "br" shouldn't match "broadcom_nic")
static int matches_virtual_prefix(const char *ifname) {
    for (int i = 0; virtual_prefixes[i] != NULL; i++) {
        size_t prefix_len = strlen(virtual_prefixes[i]);
        if (strncmp(ifname, virtual_prefixes[i], prefix_len) == 0) {
            // Check delimiter: next char must be digit, hyphen, underscore, or end
            char next_char = ifname[prefix_len];
            if (next_char == '\0' || next_char == '-' || next_char == '_' ||
                (next_char >= '0' && next_char <= '9')) {
                return 1;
            }
        }
    }
    return 0;
}

// Check if interface is a logical interface that should be shown
// (bonds, bridges, teams)
// Special handling for bridges: only brN (br0, br1, etc.) are management bridges
// Names like br-int, br-ex are OVS/container bridges and should be filtered
static int is_logical_interface(const char *ifname) {
    for (int i = 0; logical_interface_prefixes[i] != NULL; i++) {
        const char *prefix = logical_interface_prefixes[i];
        size_t prefix_len = strlen(prefix);

        if (strncmp(ifname, prefix, prefix_len) == 0) {
            char next_char = ifname[prefix_len];

            // Special case for "br": only allow brN (digit after br), not br-* or br_*
            if (strcmp(prefix, "br") == 0) {
                if (next_char >= '0' && next_char <= '9') {
                    return 1;  // br0, br1, etc. - management bridge
                }
                return 0;  // br-int, br-ex, etc. - container bridge
            }

            // For bond/team: allow digit, hyphen, underscore, or end
            if (next_char == '\0' || next_char == '-' || next_char == '_' ||
                (next_char >= '0' && next_char <= '9')) {
                return 1;
            }
        }
    }
    return 0;
}

// Returns 1 if the interface is virtual (should be hidden), 0 if physical/logical (should be shown)
// Uses readlink to check if device symlink points to /sys/devices/virtual/
// Falls back to prefix matching when sysfs is unavailable
//
// Special handling:
// - Bonds, bridges, teams, macvlan, ipvlan are treated as "physical" (return 0)
//   even though they're under /sys/devices/virtual/, because they commonly carry
//   management IPs on appliances
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
        // ENOENT = no device symlink (bridges/bonds/teams don't have one)
        if (errno == ENOENT) {
            // Check if it's a logical interface that should be shown
            if (is_logical_interface(ifname)) {
                return 0;  // Logical interface (bond/bridge/team) - show it
            }
            return 1;  // Pure virtual (loopback, etc.) - hide it
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
        // It's under /sys/devices/virtual/, but check if it's a "logical" interface
        // that should still be shown (bonds, bridges, teams, macvlan, ipvlan)
        if (is_logical_interface(ifname)) {
            return 0;  // Logical interface - show it
        }
        return 1;  // Pure virtual interface - hide it
    }

    return 0;  // Physical interface
}

#endif // NETWORK_INTERFACE_UTILS_H
