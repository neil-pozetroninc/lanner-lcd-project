# Known Issues

## Kernel Device Permissions Override

### Discovery

The devnode callback in the Linux kernel driver (`plcm_drv`) is working correctly. It is called by the kernel and sets the device node permissions to `0660` as intended. However, on some kernel versions (observed on 6.17 / Ubuntu 25.10), the kernel or a related subsystem (like udev during early boot) subsequently overrides this mode, resetting the permissions to a default of `0666`.

### Evidence from `dmesg`

```
[14.692823] plcm_drv: devnode callback registered
[14.698570] plcm_devnode callback called
[14.702951] plcm_drv: devnode mode set to 0660
```

Despite this, the device node is created with world-writable permissions:
```
crw-rw-rw- 1 root lcd 239, 0 (0666)
```

The callback is invoked multiple times during boot (as shown in logs), and each time it correctly sets `0660`. However, the final device permissions remain `0666`.

### Affected Systems

- **Observed on**: Linux Kernel 6.17.0-6-generic (Ubuntu 25.10)
- **May affect**: Other modern kernels may exhibit similar behavior

This appears to be a timing issue between devtmpfs, the kernel's device class system, and udev's rule application during early boot.

### Root Cause

The devnode callback sets the *intended* permissions, but:
1. Devtmpfs creates the device node with default permissions (0666)
2. The devnode callback's mode setting is not being honored by devtmpfs
3. Udev rules are supposed to fix permissions, but may not apply reliably at boot

This is a known quirk with device class callbacks in modern kernels where devtmpfs doesn't always respect the callback's mode parameter.

### Workaround

The provided `udev` rule (`99-plcm-drv.rules`) correctly specifies the permissions. However, due to boot-time timing issues, the rule may not be applied correctly on initial device creation.

**If the device has incorrect permissions after booting:**

```bash
sudo udevadm trigger --name-match=plcm_drv
```

This forces udev to re-evaluate the rules and apply the correct `0660` permissions and `lcd` group ownership.

**To verify permissions:**

```bash
ls -l /dev/plcm_drv
```

Should show: `crw-rw---- 1 root lcd 239, 0`

### Impact

-The `lcd-button-daemon.service` requires read/write access to `/dev/plcm_drv`
- With 0666 permissions, the device is world-writable (security concern)
- With 0660 permissions and group `lcd`, only root and members of the `lcd` group can access it (secure)
- The daemon runs as root with group `lcd`, so it works correctly once permissions are fixed

### Future Investigation

Potential solutions to investigate:
1. **Udev rule priority**: Try different rule numbering (lower numbers = higher priority)
2. **Systemd device ordering**: Ensure LCD daemon waits for udev to settle
3. **devtmpfs_mount kernel parameter**: May affect permission handling
4. **Alternative callback**: Try `dev_uevent` or other device class callbacks
5. **Kernel bug report**: This may be a regression in kernel 6.17's devtmpfs

For now, the udev rules + manual trigger workaround provides secure, functional operation.

### Related Files

- `driver/plcm_drv.c`: Devnode callback implementation (lines 609-619, 664-665)
- `udev/99-plcm-drv.rules`: Udev permission rules
- `INSTALL.md`: Installation instructions including permission verification
