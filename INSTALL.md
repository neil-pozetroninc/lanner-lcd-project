# Installation Guide - Lanner NCA-2510A LCD Project

## Prerequisites

- Modern Linux distribution (tested on Ubuntu 25.10)
- Modern kernel (tested on 6.17.0-6-generic, any 6.x+ should work)
- Build tools: gcc, make, kernel headers
- Root/sudo access

## Step-by-Step Installation

### 1. Install Build Dependencies

```bash
sudo apt-get update
sudo apt-get install -y build-essential linux-headers-$(uname -r)
```

### 2. Extract Project Files

```bash
cd ~
tar -xzf lanner-lcd-project.tar.gz
cd lanner-lcd-project
```

### 3. Create LCD Group (Required for Security)

The driver requires a dedicated `lcd` group for secure device access:

```bash
sudo groupadd -r lcd
sudo usermod -aG lcd $USER
```

Note: You'll need to log out and back in for group membership to take effect, or use `newgrp lcd` in the current shell.

The `lcd` group is required for device access by the lcd-button-daemon service. The Makefile sets permissions on `/dev/plcm_drv` as a fallback, and udev rules (if installed) will also enforce these permissions automatically.

If you skip this step, the device will be created as root:root with mode 0660 (secure but root-only), and the lcd-button-daemon service will fail to start. You must create the group and re-run `sudo make load` to fix permissions.

### 4. Build and Install Kernel Driver

```bash
cd driver/
make boot
sudo make load
```

Expected output:
```
rmmod plcm_drv || true
mknod /dev/plcm_drv c 239 0
chown root:lcd /dev/plcm_drv || true
chmod 0660 /dev/plcm_drv || true
insmod plcm_drv.ko
```

Note: If you see a warning about the 'lcd' group not existing, make sure you completed step 3 above.

Verify driver loaded:
```bash
lsmod | grep plcm_drv
# Should show: plcm_drv with size and usage count

ls -l /dev/plcm_drv
# Should show: crw-rw---- 1 root lcd 239, 0 ... /dev/plcm_drv
```

**Note**: On kernel 6.17+, the device may be created with `0666` permissions at boot despite the driver setting `0660`. If permissions are incorrect, run:
```bash
sudo udevadm trigger --name-match=plcm_drv
```
See [KNOWN_ISSUES.md](KNOWN_ISSUES.md#kernel-device-permissions-override-kernel-617) for details.

### 4.5. Install Udev Rules (Recommended for Persistent Permissions)

Install udev rules to ensure correct permissions are applied automatically:

```bash
cd ../udev
sudo cp 99-plcm-drv.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --name-match=plcm_drv
```

Verify permissions after installing rules:
```bash
ls -l /dev/plcm_drv
# Should show: crw-rw---- 1 root lcd 239, 0 ... /dev/plcm_drv
```

This ensures permissions persist across reboots and are applied automatically when the driver loads.

### 5. Make Driver Load on Boot

Create modprobe configuration:
```bash
echo "plcm_drv" | sudo tee /etc/modules-load.d/plcm_drv.conf
```

Copy module to system location:
```bash
sudo cp plcm_drv.ko /lib/modules/$(uname -r)/extra/
sudo depmod -a
```

### 6. Compile and Install Programs

```bash
cd ../programs/
gcc -O2 lcd_vitals_display_only.c -o lcd_vitals
gcc -O2 lcd_daemon_v2.c -o lcd_button_daemon

sudo cp lcd_vitals /usr/local/bin/
sudo cp lcd_button_daemon /usr/local/bin/
sudo chmod +x /usr/local/bin/lcd_vitals /usr/local/bin/lcd_button_daemon
```

Test display program:
```bash
sudo /usr/local/bin/lcd_vitals
```

The LCD should light up and show system information.

### 7. Install and Enable Systemd Service

```bash
cd ../systemd/
sudo cp lcd-button-daemon.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable lcd-button-daemon.service
sudo systemctl start lcd-button-daemon.service
```

Verify service is running:
```bash
sudo systemctl status lcd-button-daemon.service
```

Expected output:
```
● lcd-button-daemon.service - LCD Button Polling Daemon
     Loaded: loaded (/etc/systemd/system/lcd-button-daemon.service; enabled; preset: enabled)
     Active: active (running) since ...
```

### 8. Monitor Operation

Watch daemon logs:
```bash
sudo journalctl -u lcd-button-daemon.service -f
```

You should see:
- "Starting LCD daemon..."
- "LCD daemon running (PID: ...)"
- "Polling (200ms), auto-cycle (5s), display refresh (1s)"
- Periodic "Auto-cycle -> state X" messages every 5 seconds
- "LEFT button -> state X" or "RIGHT button -> state X" when buttons pressed

### 9. Test Buttons

Press the LEFT or RIGHT buttons on the LCD front panel:
- LEFT button: Cycles backward through states
- RIGHT button: Cycles forward through states

The display should immediately respond to button presses.

## Verification Checklist

- [ ] Driver loaded: `lsmod | grep plcm_drv` shows module
- [ ] Device exists: `/dev/plcm_drv` exists with major 239
- [ ] LCD backlight on
- [ ] LCD displays system vitals (load, memory, time)
- [ ] LCD line 2 cycles every 5 seconds (model → IP → hostname)
- [ ] LEFT button cycles backward
- [ ] RIGHT button cycles forward
- [ ] Service enabled: `systemctl is-enabled lcd-button-daemon.service` shows "enabled"
- [ ] Service active: `systemctl is-active lcd-button-daemon.service` shows "active"
- [ ] After reboot, everything works automatically

## Uninstallation

If you need to remove the project:

```bash
# Stop and disable service
sudo systemctl stop lcd-button-daemon.service
sudo systemctl disable lcd-button-daemon.service
sudo rm /etc/systemd/system/lcd-button-daemon.service
sudo systemctl daemon-reload

# Remove programs
sudo rm /usr/local/bin/lcd_vitals /usr/local/bin/lcd_button_daemon

# Remove driver
sudo rmmod plcm_drv
sudo rm /dev/plcm_drv
sudo rm /lib/modules/$(uname -r)/extra/plcm_drv.ko
sudo rm /etc/modules-load.d/plcm_drv.conf
sudo depmod -a

# Remove state files
sudo rm -f /var/run/lcd_cycle_state /run/lcd_button_daemon.pid
```

## Troubleshooting

### Issue: Driver fails to load with "device or resource busy"

**Solution**: Check if another driver is using the parallel port:
```bash
lsmod | grep parport
sudo rmmod parport_pc parport
```

### Issue: "Cannot allocate memory" or major number conflict

**Solution**: Check which major numbers are in use:
```bash
cat /proc/devices | grep -E "23[0-9]"
```
If 239 is taken, edit `driver/Makefile` and `driver/plcm_drv.c` to use a different free major number.

### Issue: LCD shows blocks/garbage

**Solution**: This is normal when pressing buttons (cursor characters). If all characters are blocks:
- Check contrast adjustment (hardware potentiometer)
- Verify driver is properly initialized
- Run `sudo /usr/local/bin/lcd_vitals` manually

### Issue: Buttons not responsive

**Solution**:
1. Check daemon is running: `systemctl status lcd-button-daemon.service`
2. Monitor logs: `journalctl -u lcd-button-daemon.service -f`
3. Verify button detection by watching logs while pressing buttons

### Issue: Service fails to start at boot

**Solution**:
```bash
sudo systemctl enable lcd-button-daemon.service
sudo systemctl daemon-reload
```

Check service logs:
```bash
sudo journalctl -u lcd-button-daemon.service -b
```

## Support

For issues or questions:
- Check logs: `journalctl -u lcd-button-daemon.service`
- Verify driver: `dmesg | grep plcm`
- Test manually: `sudo /usr/local/bin/lcd_vitals`
