# Udev Rules for plcm_drv

## Installation

```bash
# Create dedicated group for LCD access
sudo groupadd -r lcd

# Install udev rule
sudo cp 99-plcm-drv.rules /etc/udev/rules.d/

# Reload udev rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## Verification

```bash
# Check device permissions
ls -l /dev/plcm_drv

# Should show: crw-rw---- 1 root lcd
```

## Adding Users to LCD Group

```bash
# Add your user
sudo usermod -aG lcd $USER

# Log out and back in for group membership to take effect
```

## Security Benefits

- **Principle of least privilege**: Only users in `lcd` group can access device
- **No root required**: Daemon can run as regular user in `lcd` group
- **Audit trail**: Group membership tracked in /etc/group
