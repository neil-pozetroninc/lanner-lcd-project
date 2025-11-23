/*
 * Lanner Parallel LCM Driver Test Program - for shift cursor & update single text
 * Security-hardened version: Uses driver interface instead of iopl(3)
 */
#include <sys/file.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include "plcm_ioctl.h"

void printf_usage()
{
    printf("=====================\n");
    printf("[1] insert line:\n");
    printf("[2] move cursor right:\n");
    printf("[3] move cursor left:\n");
    printf("[4] add a char:\n");
    printf("[5] clean display\n");
    printf("[6] leave\n");
    printf("=====================\n");
}

int main(int argc, char *argv[])
{
    int devfd;
    uint8_t lcm_char;
    int input = 0;
    int line_choose = 1;
    char clean_buffer[20];
    char line[32];
    int parsed;
    int ret;

    (void)argc;
    (void)argv;

    memset(clean_buffer, 0x20, sizeof(clean_buffer));

    printf("Lanner Parallel LCM Test Program for cursor & char:\n");

    devfd = open("/dev/plcm_drv", O_RDWR);
    if (devfd == -1) {
        perror("Cannot open /dev/plcm_drv");
        return -1;
    }

    /* Display Control - Display Off */
    printf("  Display Control - Display Off\n");
    ret = ioctl(devfd, PLCM_IOCTL_DISPLAY_D, 0);
    if (ret < 0) {
        fprintf(stderr, "Warning: ioctl(PLCM_IOCTL_DISPLAY_D, 0) failed\n");
    }
    sleep(2);

    /* Display Control - Display On */
    printf("  Display Control - Display On\n");
    ret = ioctl(devfd, PLCM_IOCTL_DISPLAY_D, 1);
    if (ret < 0) {
        fprintf(stderr, "Warning: ioctl(PLCM_IOCTL_DISPLAY_D, 1) failed\n");
    }
    sleep(2);

    /* Display Control - Blinking off */
    printf("  Display Control - Blinking off\n");
    ret = ioctl(devfd, PLCM_IOCTL_DISPLAY_B, 0);
    if (ret < 0) {
        fprintf(stderr, "Warning: ioctl(PLCM_IOCTL_DISPLAY_B, 0) failed\n");
    }
    sleep(2);

    /* Display Control - Cursor On */
    printf("  Display Control - Cursor On\n");
    ret = ioctl(devfd, PLCM_IOCTL_DISPLAY_C, 1);
    if (ret < 0) {
        fprintf(stderr, "Warning: ioctl(PLCM_IOCTL_DISPLAY_C, 1) failed\n");
    }
    sleep(2);

    ret = ioctl(devfd, PLCM_IOCTL_SET_LINE, 1);
    if (ret < 0) {
        fprintf(stderr, "Warning: ioctl(PLCM_IOCTL_SET_LINE, 1) failed\n");
    }

    while (input != 6) {
        printf_usage();
        printf("please input one mode: ");

        if (fgets(line, sizeof(line), stdin) == NULL) {
            fprintf(stderr, "Input error\n");
            continue;
        }

        if (sscanf(line, "%d", &parsed) != 1) {
            fprintf(stderr, "Invalid integer\n");
            continue;
        }

        if (parsed < 1 || parsed > 6) {
            fprintf(stderr, "Value out of range (1-6)\n");
            continue;
        }

        input = parsed;

        if (input == 1) {
            printf("[1] select line 1, [2] select line 2: ");

            if (fgets(line, sizeof(line), stdin) == NULL) {
                fprintf(stderr, "Input error\n");
                continue;
            }

            if (sscanf(line, "%d", &parsed) != 1) {
                fprintf(stderr, "Invalid integer\n");
                continue;
            }

            if (parsed == 1) {
                ret = ioctl(devfd, PLCM_IOCTL_SET_LINE, 1);
                if (ret < 0) {
                    fprintf(stderr, "Error: Failed to set line 1\n");
                } else {
                    line_choose = 1;
                }
            } else if (parsed == 2) {
                ret = ioctl(devfd, PLCM_IOCTL_SET_LINE, 2);
                if (ret < 0) {
                    fprintf(stderr, "Error: Failed to set line 2\n");
                } else {
                    line_choose = 2;
                }
            } else {
                printf("Invalid line choice\n");
            }
        } else if (input == 2) {
            printf("cursor right\n");
            ret = ioctl(devfd, PLCM_IOCTL_SHIFT_RL, 1);
            if (ret < 0) {
                fprintf(stderr, "Error: Failed to move cursor right\n");
            }
        } else if (input == 3) {
            printf("cursor left\n");
            ret = ioctl(devfd, PLCM_IOCTL_SHIFT_RL, 0);
            if (ret < 0) {
                fprintf(stderr, "Error: Failed to move cursor left\n");
            }
        } else if (input == 4) {
            printf("input a char: ");

            if (fgets(line, sizeof(line), stdin) == NULL) {
                fprintf(stderr, "Input error\n");
                continue;
            }

            if (line[0] == '\0' || line[0] == '\n') {
                fprintf(stderr, "No character entered\n");
                continue;
            }

            lcm_char = line[0];

            printf("line is %d\n", line_choose);
            ret = ioctl(devfd, PLCM_IOCTL_INPUT_CHAR, (unsigned long)lcm_char);
            if (ret < 0) {
                fprintf(stderr, "Error: Failed to input character\n");
            }
        } else if (input == 5) {
            printf("clear display:\n");
            ret = ioctl(devfd, PLCM_IOCTL_SET_LINE, 1);
            if (ret < 0) {
                fprintf(stderr, "Error: Failed to set line 1 for clearing\n");
            }
            ret = write(devfd, clean_buffer, sizeof(clean_buffer));
            if (ret < 0) {
                fprintf(stderr, "Error: Failed to write clear buffer to line 1\n");
            }
            ret = ioctl(devfd, PLCM_IOCTL_SET_LINE, 2);
            if (ret < 0) {
                fprintf(stderr, "Error: Failed to set line 2 for clearing\n");
            }
            ret = write(devfd, clean_buffer, sizeof(clean_buffer));
            if (ret < 0) {
                fprintf(stderr, "Error: Failed to write clear buffer to line 2\n");
            }
            ret = ioctl(devfd, PLCM_IOCTL_RETURNHOME, 0);
            if (ret < 0) {
                fprintf(stderr, "Error: Failed to return home\n");
            }
        } else if (input == 6) {
            printf("leaving test\n");
        }
    }

    close(devfd);
    return 0;
}
