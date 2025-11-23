#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define PLCM_IOCTL_GET_KEYPAD 0x0C

int main() {
    int fd = open("/dev/plcm_drv", O_RDWR);
    if (fd < 0) return 1;
    
    int last = 0;
    printf("Press UP and DOWN buttons (Ctrl+C to exit)...\n");
    
    while (1) {
        int val = ioctl(fd, PLCM_IOCTL_GET_KEYPAD, 0);
        if (val != last && (val & 0x40) != 0) {
            printf("Button pressed: 0x%02X\n", val);
            fflush(stdout);
        }
        if ((val & 0x40) == 0) last = val;
        usleep(50000);
    }
    
    close(fd);
    return 0;
}
