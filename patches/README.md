# Patches for plcm_drv_v013

This directory contains patches to upgrade the original [plcm_drv_v013](https://github.com/majodu/plcm_drv_v013) repository to work with modern kernels (6.x+) and add security hardening.

## Applying Patches

From the original repository directory:

```bash
# Apply driver changes
patch -p0 < patches/plcm_drv-device-creation-full.patch

# Apply Makefile changes
patch -p0 < patches/makefile-hardening-full.patch
```

## Additional Files Required

The patches update existing files, but you also need these new files from this repository:

### Udev Rules
Copy `udev/99-plcm-drv.rules` to `/etc/udev/rules.d/`:
```bash
sudo cp udev/99-plcm-drv.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
```

### Systemd Service
Copy `systemd/lcd-button-daemon.service` to `/etc/systemd/system/`:
```bash
sudo cp systemd/lcd-button-daemon.service /etc/systemd/system/
sudo systemctl daemon-reload
```

### Programs
Compile and install the user-space programs:
```bash
cd programs/
gcc -O2 lcd_vitals_display_only.c -o lcd_vitals
gcc -O2 lcd_daemon_v2.c -o lcd_button_daemon
sudo cp lcd_vitals /usr/local/bin/
sudo cp lcd_button_daemon /usr/local/bin/
sudo chmod +x /usr/local/bin/lcd_vitals /usr/local/bin/lcd_button_daemon
```

### Documentation
- `INSTALL.md`: Comprehensive installation guide
- `KNOWN_ISSUES.md`: Documents kernel 6.17+ permission override behavior

## What Changed

### plcm_drv-device-creation-full.patch
- Added `<linux/ioport.h>` and `<linux/device.h>` includes
- Changed `<asm/uaccess.h>` to `<linux/uaccess.h>` for kernel 6.x compatibility
- Changed major number from 248 to 239
- Added I/O port reservation with `request_region()`
- Added device class creation with `class_create()`
- Added devnode callback `plcm_devnode()` for setting 0660 permissions
- Added device creation with `device_create()`
- Added proper cleanup in init failure paths and module exit
- Added diagnostic printk statements
- Added KNOWN_ISSUES.md references in comments

### makefile-hardening-full.patch
- Added kernel module hardening flags comments
- Added user-space hardening flags (stack protector, PIE, FORTIFY_SOURCE, RELRO, NX stack)
- Added `load` target with lcd group check and permission setting
- Updated comments for kernel 2.6.x and newer

## Complete Migration Path

1. Clone original repository:
   ```bash
   git clone https://github.com/majodu/plcm_drv_v013
   cd plcm_drv_v013
   ```

2. Apply patches:
   ```bash
   patch -p0 < /path/to/patches/plcm_drv-device-creation-full.patch
   patch -p0 < /path/to/patches/makefile-hardening-full.patch
   ```

3. Copy additional files from this repository (udev, systemd, programs)

4. Follow `INSTALL.md` for complete installation instructions

## Original Files (`.orig`)

The `*.orig` files in the `driver/` directory preserve the original upstream code before patches were applied. These files serve as reference points for:
- Creating and updating patches when the upstream changes
- Understanding what modifications were made
- Regenerating patches if needed

These are intentionally kept in the repository for patch maintenance purposes.
