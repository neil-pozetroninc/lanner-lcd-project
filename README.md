# Lanner NCA-2510A LCD Display Project

This project implements system vitals display on the Lanner NCA-2510A network appliance LCD (20x2 HD44780 parallel port display).

This project is tested on modern Linux kernel versions (6.x+), though it may work on older kernels with appropriate adjustments.

Installation instructions cover multiple distributions (Ubuntu, Fedora, Arch Linux, SUSE). The code is distribution-agnostic and will work on any Linux distribution with appropriate kernel headers and build tools.

## Features

### Multistate Display System
- **Line 1** (4 states, 10-second auto-cycle, UP/DOWN button control):
  - State 0: Load average, Memory usage, Current time
  - State 1: CPU temperature, Disk usage, System uptime
  - State 2: Network RX/TX statistics (real-time monitoring)
  - State 3: Uptime, Process count, Swap usage

- **Line 2** (dynamic states, 5-second auto-cycle, LEFT/RIGHT button control):
  - State 0: Model name (Lanner NCA-2510A)
  - States 1-N: Individual IP addresses for each physical interface
  - State N+1: Hostname

### Advanced Features
- Independent auto-cycling for both lines (line 1: 10s, line 2: 5s)
- Fast polling daemon (200ms) for responsive button detection
- All 4 front panel buttons functional (UP, DOWN, LEFT, RIGHT)
- Dynamic IP detection (automatically adjusts when interfaces change)
- Valid CPU temperature reading with thermal zone filtering
- System monitoring: load, memory, disk, CPU temp, uptime, processes, swap
- Systemd service for automatic startup

## Hardware

- **Device**: Lanner NCA-2510A
- **LCD**: 20x2 character display (HD44780 controller)
- **Interface**: Parallel port (LPT1 at I/O 0x378)
- **Buttons**: UP=0xC7, DOWN=0xCF, LEFT=0xEF, RIGHT=0xE7
- **Driver**: Custom plcm_drv kernel module

## Components

### 1. Kernel Driver (`driver/`)

Custom Lanner parallel port LCD driver, patched for kernel 6.x:
- `plcm_drv.c` - Main driver source
- `Makefile` - Build configuration
- Major number: 239 (changed from 248 to avoid conflict)
- Device: /dev/plcm_drv

**Key patches applied**:
- asm/uaccess.h → linux/uaccess.h
- file_operations.ioctl → unlocked_ioctl
- Major number 248 → 239
- Modern Kbuild system

### 2. Patches (`patches/`)

Comprehensive patch set for modernizing the driver:
- `plcm_drv-device-creation-full.patch` - Driver modernization (device creation, kernel 6.x compatibility, I/O port reservation, devnode callback, diagnostic messages)
- `makefile-hardening-full.patch` - Makefile updates (hardening flags, lcd group checks, load target)
- See `patches/README.md` for detailed change descriptions and migration path

### 3. Programs (`programs/`)

**lcd_vitals_multistate.c** - Display program with 4-state line 1 support
- Reads two state files: /var/run/lcd_line1_state and /var/run/lcd_cycle_state
- Displays 4 different metric views on line 1
- Dynamically displays all IP addresses on line 2
- CPU temperature filtering (skips invalid thermal zones)
- Real-time network RX/TX monitoring with rate calculation
- Installed to: /usr/local/bin/lcd_vitals

**lcd_daemon_multistate.c** - Dual auto-cycling daemon
- 200ms polling interval for button detection
- Independent auto-cycling: 10s for line 1, 5s for line 2
- 1 second display refresh
- All 4 buttons functional (UP/DOWN for line 1, LEFT/RIGHT for line 2)
- Installed to: /usr/local/bin/lcd_button_daemon

**identify_updown.c** - Button code detection utility
- Detects UP, DOWN, LEFT, and RIGHT button codes
- Used during development to identify hardware button values

**Earlier versions** (for reference):
- `lcd_vitals_display_only.c` - Single-line cycling version
- `lcd_daemon_v2.c` - Single-line cycling daemon
- `lcd_vitals_buttons.c` - Standalone version with button support

### 4. Systemd Service (`systemd/`)

`lcd-button-daemon.service` - Daemon service configuration
- Auto-starts at boot with device ordering
- Type=forking with PID file
- Runs as root with lcd group for device access
- Nice 5 for low priority
- Restart on failure
- Comprehensive security hardening (NoNewPrivileges, ProtectSystem, ProtectKernelTunables, syscall filtering, memory protections, namespace isolation, capability restrictions)

### 5. Udev Rules (`udev/`)

`99-plcm-drv.rules` - Device permissions configuration
- Sets /dev/plcm_drv to 0660 permissions
- Assigns lcd group ownership
- Ensures secure device access at boot

## Installation

### Prerequisites

**IMPORTANT - Create the `lcd` Group First:**

The `lcd` group is **required** for secure device access. Without this group:
- The `/dev/plcm_drv` device will be root-only (fallback permissions)
- The `lcd-button-daemon` systemd service will fail to start
- Users will need root privileges to access the LCD

Create the group and add your user:
```bash
sudo groupadd -r lcd
sudo usermod -aG lcd $USER
```

**Note**: You will need to log out and back in for group membership to take effect. See [INSTALL.md](INSTALL.md#3-create-lcd-group-required-for-security) for details.

### 0. Build Tools (If Driver Needs Recompilation)

**IMPORTANT**: On production systems, build tools can be removed to save space (~1.2GB) after driver compilation. Kernel packages should be held to prevent automatic updates that would break the LCD driver.

If you need to rebuild the driver (e.g., after deliberately updating the kernel):

**Ubuntu/Debian:**
```bash
sudo apt-get install build-essential gcc make linux-headers-$(uname -r)
```

**Fedora/RHEL:**
```bash
sudo dnf install gcc make kernel-devel
```

**Arch Linux:**
```bash
sudo pacman -S gcc make linux-headers
```

**SUSE:**
```bash
sudo zypper install gcc make kernel-devel
```

**Kernel Hold**: To prevent automatic kernel updates that would require driver recompilation, see the [Production Deployment Notes](#production-deployment-notes) section below for distribution-specific commands to hold/lock kernel packages.

This works really well for LTS releases where kernels have a long shelf life.

**To check held/locked packages:**
- Ubuntu/Debian: `sudo apt-mark showhold`
- Fedora/RHEL: `sudo dnf versionlock list`
- Arch Linux: `grep IgnorePkg /etc/pacman.conf`
- SUSE: `sudo zypper locks`

### 1. Build and Install Driver

```bash
cd driver/
make boot
sudo make load
```

Verify driver loaded:
```bash
lsmod | grep plcm_drv
ls -l /dev/plcm_drv
```

### 2. Compile Programs

```bash
cd programs/
gcc -O2 lcd_vitals_multistate.c -o lcd_vitals
gcc -O2 lcd_daemon_multistate.c -o lcd_button_daemon

sudo cp lcd_vitals /usr/local/bin/
sudo cp lcd_button_daemon /usr/local/bin/
sudo chmod +x /usr/local/bin/lcd_vitals /usr/local/bin/lcd_button_daemon
```

### 3. Install Systemd Service

```bash
sudo cp systemd/lcd-button-daemon.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable lcd-button-daemon.service
sudo systemctl start lcd-button-daemon.service
```

Verify service running:
```bash
sudo systemctl status lcd-button-daemon.service
sudo journalctl -u lcd-button-daemon.service -f
```

## Technical Details

### Button Detection Logic

- Idle state: 0xAF (hardware default)
- Button press detected when:
  - Value changes from last reading
  - Bit 0x40 is SET (indicates button press)
- UP button: 0xC7 (cycles line 1 backward)
- DOWN button: 0xCF (cycles line 1 forward)
- LEFT button: 0xEF (cycles line 2 backward)
- RIGHT button: 0xE7 (cycles line 2 forward)
- Save keypad state only when NOT pressing (bit 0x40 == 0)

### Display Cycle States

**Line 1 (System Metrics)**:
- State 0: `L:0.52 M:45% 14:23:15` (Load, Memory %, Time)
- State 1: `CPU:40C D:58% /3d12h` (CPU temp, Disk %, Uptime)
- State 2: `RX:69K TX:1K` (Network stats - real-time bytes/sec)
- State 3: `Up:3d12h P:245 S:0%` (Uptime, Processes, Swap %)

**Line 2 (Identity)**:
- State 0: `Lanner NCA-2510A` (Model name)
- State 1: `enp2s0:10.0.0.100` (First IP)
- State 2: `enp3s0:10.0.0.1` (Second IP, if present)
- State N+1: `Host: hostname` (Hostname)

Dynamically adjusts: If you have 2 IPs, line 2 has 4 states total.

### CPU Temperature Monitoring

The `get_cpu_temp()` function:
- Iterates through /sys/class/thermal/thermal_zone*/temp
- Skips invalid readings (0°C or >150°C, usually disabled sensors)
- Returns first valid temperature in Celsius
- Falls back to hiding CPU temp if no valid sensor found

### Network Traffic Monitoring

**Auto-Detection**: Network interfaces are automatically detected at runtime.
The system scans `/sys/class/net` for active physical interfaces and automatically excludes:
- Loopback (`lo`)
- Docker interfaces (`docker*`)
- Virtual Ethernet (`veth*`)
- Bridge interfaces (`br-*`)

The `get_network_rates()` function provides real-time RX/TX statistics:
- Reads byte counters from `/sys/class/net/*/statistics/rx_bytes` and `tx_bytes`
- Stores previous values in `/var/run/lcd_net_stats` for rate calculation
- Calculates rate: (current_bytes - previous_bytes) / time_delta
- Dynamic formatting with suffixes:
  - B: bytes/sec (< 1024)
  - K: kilobytes/sec (< 1024*1024)
  - M: megabytes/sec (>= 1024*1024)
- Updates every second via daemon refresh cycle
- Example displays: `RX:243B TX:419B`, `RX:69K TX:1K`, `RX:1.2M TX:500K`

### Resource Efficiency

- clock_nanosleep(CLOCK_MONOTONIC) for precise timing
- Open/close device each poll cycle (allows display updates)
- Nice +5 (low priority)
- Limited file descriptors (64)
- Minimal CPU usage (~0.1%)
- Independent timers for each line prevent interference

### Driver IOCTL Commands

```c
#define PLCM_IOCTL_STOP_THREAD  0x00
#define PLCM_IOCTL_BACKLIGHT    0x01
#define PLCM_IOCTL_CLEARDISPLAY 0x03
#define PLCM_IOCTL_RETURNHOME   0x04
#define PLCM_IOCTL_ENTRYMODE_ID 0x05
#define PLCM_IOCTL_ENTRYMODE_SH 0x06
#define PLCM_IOCTL_DISPLAY_D    0x07
#define PLCM_IOCTL_DISPLAY_C    0x08
#define PLCM_IOCTL_DISPLAY_B    0x09
#define PLCM_IOCTL_SHIFT_SC     0x0A
#define PLCM_IOCTL_SHIFT_RL     0x0B
#define PLCM_IOCTL_GET_KEYPAD   0x0C
#define PLCM_IOCTL_SET_LINE     0x0D
#define PLCM_IOCTL_INPUT_CHAR   0x0E
```

## Tested On

- **OS**: Ubuntu 25.10 (should work on any modern Linux distribution)
- **Kernel**: 6.17.0-6-generic (any modern 6.x+ kernel should work)
- **Architecture**: x86_64

### Production Deployment Notes

For space-constrained production systems:

**Optional: Remove build tools after compilation (~1.2GB saved):**

**Ubuntu/Debian:**
```bash
sudo apt-get autoremove --purge -y build-essential gcc g++ make linux-headers-*
```

**Fedora/RHEL:**
```bash
sudo dnf remove gcc make kernel-devel
```

**Arch Linux:**
```bash
sudo pacman -Rs gcc make linux-headers
```

**SUSE:**
```bash
sudo zypper remove gcc make kernel-devel
```

**Recommended: Hold kernel packages to prevent auto-updates:**

**Ubuntu/Debian:**
```bash
sudo apt-mark hold linux-image-$(uname -r) linux-image-generic linux-modules-$(uname -r)
```

**Fedora/RHEL:**
```bash
sudo dnf versionlock add kernel kernel-core kernel-modules
```

**Arch Linux:**
```bash
# Edit /etc/pacman.conf and add under [options]:
# IgnorePkg = linux linux-headers
# Or use this command:
echo -e "\n# Hold kernel packages\nIgnorePkg = linux linux-headers" | sudo tee -a /etc/pacman.conf
```

**SUSE:**
```bash
sudo zypper addlock kernel-default
```

**Why hold the kernel:** Kernel updates require recompiling the plcm_drv module. By holding the kernel, the LCD continues working indefinitely without build tools.

**To update the kernel later:**
1. Install build tools (see Prerequisites section)
2. Unhold/unlock kernel packages using your distribution's method
3. Update system
4. Rebuild driver: `cd driver/ && make clean && make boot && sudo make load`
5. Re-hold/re-lock new kernel version

## Troubleshooting

### LCD not displaying
1. Check backlight: Should be visible
2. Verify driver loaded: `lsmod | grep plcm_drv`
3. Check device exists: `ls -l /dev/plcm_drv`
4. Test with plcm_test: `cd driver && sudo ./plcm_test`

### Buttons not working
1. Check daemon running: `systemctl status lcd-button-daemon.service`
2. Monitor logs: `journalctl -u lcd-button-daemon.service -f`
3. Verify button codes with identify_updown utility
4. Press buttons and watch for log entries like "UP button -> line1 state 1/4"

### Auto-cycling not working
1. Check daemon logs for "Auto-cycle" messages
2. Verify state files exist:
   - `cat /var/run/lcd_line1_state`
   - `cat /var/run/lcd_cycle_state`
3. Check display refresh is running (should update every 1 second)

### CPU temperature showing 0°C or missing
1. Check available thermal zones: `cat /sys/class/thermal/thermal_zone*/temp`
2. The code automatically skips invalid sensors (0 or >150000 millidegrees)
3. If no valid sensor found, CPU temp won't be displayed

### Wrong number of IP addresses
1. System dynamically counts physical interfaces only
2. Skips: loopback (lo), docker, veth, br- interfaces
3. Verify interfaces: `ip addr show`
4. Restart daemon to re-detect: `sudo systemctl restart lcd-button-daemon`

## Development Notes

### Button Code Discovery
Used `identify_updown.c` to discover actual button codes:
- UP button: 0xC7 (not the initial guess of 0xDF)
- DOWN button: 0xCF (not the initial guess of 0xBF)

### Thermal Zone Filtering
Originally read first thermal zone unconditionally, which returned 0°C from disabled sensor.
Now iterates through zones and validates readings (> 0°C and < 150°C).

### Network Monitoring Implementation
Added real-time network traffic monitoring to replace placeholder in state 2:
- Implemented stateful tracking using /var/run/lcd_net_stats
- Calculates bytes/sec by comparing current and previous byte counters
- Dynamic formatting (B/K/M suffixes) based on traffic volume
- Automatically detects active network interface
- Verified accuracy with sustained traffic tests

## Credits

Developed for Lanner NCA-2510A network appliance
Driver originally from Lanner Inc., patched for modern kernels

## License

This project is dual-licensed under BSD 2-Clause License or GNU General Public License v2.0.
See the LICENSE file for full license text.

Original driver code: Copyright (c) Lanner Inc. (Lannerinc)
Enhancements and modifications: Copyright (c) 2025 Neil @ Pozetron Inc.

## LIMITATION OF LIABILITY

UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING, LICENSOR PROVIDES THE WORK ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING, WITHOUT LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. YOU ARE SOLELY RESPONSIBLE FOR DETERMINING THE APPROPRIATENESS OF USING OR REDISTRIBUTING THE WORK AND ASSUME ANY RISKS ASSOCIATED WITH YOUR EXERCISE OF PERMISSIONS UNDER THIS LICENSE.

IN NO EVENT AND UNDER NO LEGAL THEORY, WHETHER IN TORT (INCLUDING NEGLIGENCE), CONTRACT, OR OTHERWISE, UNLESS REQUIRED BY APPLICABLE LAW (SUCH AS DELIBERATE AND GROSSLY NEGLIGENT ACTS) OR AGREED TO IN WRITING, SHALL THE AUTHOR BE LIABLE TO YOU FOR DAMAGES, INCLUDING ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES OF ANY CHARACTER ARISING AS A RESULT OF THIS LICENSE OR OUT OF THE USE OR INABILITY TO USE THE WORK (INCLUDING BUT NOT LIMITED TO DAMAGES FOR LOSS OF GOODWILL, WORK STOPPAGE, COMPUTER FAILURE OR MALFUNCTION, OR ANY AND ALL OTHER COMMERCIAL DAMAGES OR LOSSES), EVEN IF THE AUTHOR HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
