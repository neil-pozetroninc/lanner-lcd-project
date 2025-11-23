# Kernel Version Check
KVER  := $(shell uname -r | cut -c1-3 | sed 's/2\.[56]/2\.6/')
KVERSUB := $(shell uname -r | cut -c5-6)
KVER3 :=$(shell uname -r | cut -c1-1)
# Makefile for a basic kernel module - 2.4.x
KINC=
ifeq ($(KINC),)
KINC  := $(shell uname -r)
endif
CC    := gcc
MODCFLAGS := -DMODULE -D__KERNEL__ -DLINUX -DOLDKERNEL -O -I/usr/src/linux-$(KINC)/include

# Makefile for a basic kernel module - 2.6.x and newer
obj-m := plcm_drv.o
KDIR  := /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

# Kernel module hardening flags
# Note: Most hardening (stack protector, FORTIFY_SOURCE, retpoline, CFI, etc.)
# is inherited automatically from the kernel's own KBUILD_CFLAGS and config.
# Manually adding flags can cause ABI mismatches or build failures.
# The kernel build system already applies appropriate hardening based on
# CONFIG_FORTIFY_SOURCE, CONFIG_STACKPROTECTOR*, CONFIG_HARDENED_USERCOPY, etc.
#
# We rely on the kernel's hardening configuration (see docs/plans/ for recommended
# kernel config options like CONFIG_STRICT_KERNEL_RWX, CONFIG_HARDENED_USERCOPY).
#
# Only add flags here if absolutely necessary and use cc-option to check support:
# ccflags-y += $(call cc-option,-Wextra-hardening-flag)

# User-space test programs with hardening
HARDENING_CFLAGS = -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -O2 -Wall -Wextra \
                   -Wformat=2 -Wformat-security -fstack-clash-protection
HARDENING_LDFLAGS = -pie -Wl,-z,relro,-z,now,-z,noexecstack

default:
	$(CC) $(HARDENING_CFLAGS) ppdev_test.c -o ppdev_test $(HARDENING_LDFLAGS)
	$(CC) $(HARDENING_CFLAGS) plcm_test.c -o plcm_test $(HARDENING_LDFLAGS)
	$(CC) $(HARDENING_CFLAGS) plcm_cursor_char.c -o plcm_cursor_char $(HARDENING_LDFLAGS)  

boot:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
ifeq ($(wildcard lcd-menu),)
	$(CC) $(HARDENING_CFLAGS) ppdev_test.c -o ppdev_test $(HARDENING_LDFLAGS)
	$(CC) $(HARDENING_CFLAGS) plcm_test.c -o plcm_test $(HARDENING_LDFLAGS)
	$(CC) $(HARDENING_CFLAGS) plcm_cursor_char.c -o plcm_cursor_char $(HARDENING_LDFLAGS)
endif

load:
	@if ! getent group lcd > /dev/null 2>&1; then \
		echo "WARNING: 'lcd' group does not exist. Device will be root-only."; \
		echo "The lcd-button-daemon service will fail to start."; \
		echo "Create the group with: sudo groupadd -r lcd"; \
	fi
	rmmod plcm_drv || true
ifeq ($(wildcard /dev/plcm_drv),)
	mknod /dev/plcm_drv c 239 0
endif
	chown root:lcd /dev/plcm_drv || true
	chmod 0660 /dev/plcm_drv || true
	insmod plcm_drv.ko	
	
clean:
	rm -f plcm_test
	rm -f plcm_cursor_char
	rm -f ppdev_test
	$(MAKE) -C $(KDIR) M=$(PWD) clean
