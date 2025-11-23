#ifndef INPUT_VALIDATION_H
#define INPUT_VALIDATION_H

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <net/if.h>

/* Validate file path is within expected directories */
static inline int validate_path(const char *path) {
    char resolved[PATH_MAX];

    /* Null check to prevent crash */
    if (path == NULL) {
        return -1;
    }

    if (realpath(path, resolved) == NULL) {
        return -1;
    }

    /* Only allow /proc, /sys, /var/run */
    if (strncmp(resolved, "/proc/", 6) != 0 &&
        strncmp(resolved, "/sys/", 5) != 0 &&
        strncmp(resolved, "/var/run/", 9) != 0) {
        return -1;
    }

    return 0;
}

/* Parse temperature with range validation */
static inline int parse_temp(const char *str, int *temp) {
    char *endptr;
    long val;

    if (str == NULL || temp == NULL) {
        return -1;
    }

    /* Clear errno before strtol to detect overflow */
    errno = 0;
    val = strtol(str, &endptr, 10);

    /* Check for overflow/underflow */
    if (errno == ERANGE) {
        return -1;
    }

    /* Reject partial parsing or non-numeric input */
    if (endptr == str || *endptr != '\0') {
        return -1;  /* Parse error */
    }

    /* Temperature range validation (millidegrees: -50C to +150C) */
    if (val < -50000 || val > 150000) {
        return -1;  /* Invalid temperature range */
    }

    *temp = (int)val;
    return 0;
}

/* Read and validate state file */
static inline int read_state_file(const char *path, int *state, int max_state) {
    char buf[32];  /* Increased buffer size for safety */
    int fd;
    ssize_t n;
    char *endptr;
    long val;

    if (path == NULL || state == NULL) {
        return -1;
    }

    /* Validate max_state is reasonable */
    if (max_state < 0 || max_state > 1000000) {
        return -1;
    }

    /* Validate path is in allowed directories */
    if (validate_path(path) != 0) {
        return -1;
    }

    /* Open with O_NOFOLLOW to prevent symlink attacks */
    fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        return -1;
    }

    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) {
        return -1;
    }

    buf[n] = '\0';

    /* Clear errno before strtol */
    errno = 0;
    val = strtol(buf, &endptr, 10);

    /* Check for overflow */
    if (errno == ERANGE) {
        return -1;
    }

    /* Reject non-numeric input (allow newline at end) */
    if (endptr == buf) {
        return -1;  /* No digits parsed */
    }

    /* Allow trailing whitespace/newline but nothing else */
    while (*endptr == ' ' || *endptr == '\t' || *endptr == '\n' || *endptr == '\r') {
        endptr++;
    }

    if (*endptr != '\0') {
        return -1;  /* Non-whitespace garbage after number */
    }

    /* Validate range */
    if (val < 0 || val > max_state) {
        return -1;
    }

    *state = (int)val;
    return 0;
}

/* Validate network interface name (allowlist) */
static inline int is_valid_interface(const char *ifname) {
    size_t len;
    size_t i;

    if (ifname == NULL) {
        return 0;
    }

    /* Check length (IFNAMSIZ is typically 16) */
    len = strlen(ifname);
    if (len == 0 || len >= IFNAMSIZ) {
        return 0;
    }

    /* Validate character set: alphanumeric, underscore, hyphen, colon only */
    for (i = 0; i < len; i++) {
        char c = ifname[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == ':' || c == '.')) {
            return 0;  /* Invalid character (blocks path traversal) */
        }
    }

    /* Allow: enp*, eth* */
    if (strncmp(ifname, "enp", 3) == 0) return 1;
    if (strncmp(ifname, "eth", 3) == 0) return 1;

    /* Block: lo, docker*, veth*, br-*, and all others */
    return 0;
}

#endif /* INPUT_VALIDATION_H */
