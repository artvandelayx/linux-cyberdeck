# Linux Cyberdeck

A standalone Android APK that turns an Android phone into a Linux desktop appliance with Debian 13, XFCE, Firefox, and development tools.

## Features

- **Complete Debian 13 (Trixie) ARM64 userspace** - bash, coreutils, python3, git, ssh, curl, wget, nano, and more
- **XFCE Desktop Environment** - Touch-optimized with larger icons, fonts, and window controls
- **X11 Display Server** - Xvfb + Xwayland running on display :1
- **Firefox ESR** - Full web browser with HTTPS, downloads, and file access
- **Development Tools** - Python 3, pip, venv, git, OpenSSH client/server
- **Persistent Filesystem** - Linux environment survives reboots and app restarts
- **Android Storage Integration** - Access shared storage and SD card from Linux at `/mnt/shared` and `/mnt/sdcard`
- **Process Supervision** - Automatic restart of crashed components (X11, XFCE, Debian)
- **Auto-start on Boot** - Optional foreground service to start Linux at phone boot
- **No Root Required** - Works on stock Android with locked bootloader
- **No Termux Dependency** - Self-contained PRoot-based userspace

## Target Device

- **Motorola Moto G 2025 XT2513V** (ARM64)
- Android 14+ (API 34)
- Minimum 4GB RAM, 8GB free storage recommended

## Screenshots

*(Screenshots to be added)*

## Installation

1. Download the latest `LinuxCyberdeck.apk` from [Releases](https://github.com/yourusername/linux-cyberdeck/releases)
2. Install the APK on your Android device
3. Grant storage permissions when prompted
4. Launch "Linux Cyberdeck"
5. Tap **START LINUX** - first run will download ~4GB rootfs
6. Wait for installation to complete
7. Tap **START LINUX** again to launch the desktop

## Usage

### Main Screen

```
LINUX CYBERDECK
Debian 13 • XFCE • ARM64

● Linux stopped

[ START LINUX ]

[ Files ] [ Terminal ] [ Firefox ]
Settings
```

### Running State

```
LINUX CYBERDECK
Debian 13 • XFCE • ARM64

● Linux running

[ OPEN DESKTOP ]
[ TERMINAL ] [ FIREFOX ] [ FILES ]
[ RESTART LINUX ] [ STOP LINUX ]
Settings
```

### Terminal

```
cyber@cyberdeck:~$ python3 --version
Python 3.11.6

cyber@cyberdeck:~$ git --version
git version 2.43.0

cyber@cyberdeck:~$ curl https://example.com
<!doctype html>...
```

### Storage Access

Linux can access Android storage at:
- `/mnt/shared` - Internal shared storage (Downloads, Documents, Pictures, etc.)
- `/mnt/sdcard` - Removable SD card (if present)

### Local Web Server

```bash
python3 -m http.server 8080
# Access at http://localhost:8080 from Firefox
```

## Architecture

```
┌─────────────────────────────────────┐
│         Android Application         │
│  ┌───────────────────────────────┐  │
│  │     LinuxSessionManager       │  │
│  │  (State machine + UI bridge)  │  │
│  └───────────────┬────────────────┘  │
└──────────────────┼────────────────────┘
                   │ JNI
┌──────────────────▼────────────────────┐
│           Native Layer (C)            │
│  ┌──────────┐ ┌──────────┐ ┌────────┐ │
│  │  PRoot   │ │  Xvfb    │ │ Xwayland│ │
│  │ (userspace│ │(framebuf)│ │(X11 on │ │
│  │ isolation)│ │          │ │ Wayland)│ │
│  └────┬─────┘ └────┬─────┘ └────┬───┘ │
│       │            │            │      │
│  ┌────▼────────────▼────────────▼────┐ │
│  │     Linux Session Supervisor      │ │
│  │ (process monitor, recovery, logs) │ │
│  └────────────────────┬──────────────┘ │
└───────────────────────┼────────────────┘
                        │ PRoot
┌───────────────────────▼────────────────┐
│      Debian 13 ARM64 Rootfs             │
│  ┌────────┐ ┌──────┐ ┌──────┐ ┌──────┐  │
│  │  XFCE  │ │Firefox│ │ Python│ │  Git  │  │
│  │ Desktop│ │ Browser│ │  3.11 │ │  2.43 │  │
│  └────────┘ └──────┘ └──────┘ └──────┘  │
│  ┌────────────────────────────────────┐  │
│  │     Storage Bridge (FUSE)          │  │
│  │  /mnt/shared  /mnt/sdcard          │  │
│  └────────────────────────────────────┘  │
└──────────────────────────────────────────┘
```

## Building from Source

### Prerequisites

- Linux build environment (WSL2 on Windows works)
- Android NDK r26+
- debootstrap, qemu-user-static
- 20GB+ free disk space

### Build Steps

```bash
# Clone repository
git clone https://github.com/yourusername/linux-cyberdeck.git
cd linux-cyberdeck

# Build Debian rootfs (run on Linux/WSL2)
./scripts/build_rootfs.sh

# Open in Android Studio
# Build > Build Bundle(s) / APK(s) > Build APK(s)

# Or build via command line
./gradlew assembleRelease
```

Output: `app/build/outputs/apk/release/app-release.apk`

See [BUILD.md](BUILD.md) for detailed instructions.

## Documentation

- [Architecture](ARCHITECTURE.md) - Technical architecture deep dive
- [Build Guide](BUILD.md) - Step-by-step build instructions
- [Development](DEVELOPMENT.md) - Contributing guidelines

## License

GPL-3.0-or-later - See [LICENSE](LICENSE) for details.

This project bundles several open-source components:
- PRoot (GPL-2.0)
- Debian (various licenses)
- XFCE (GPL-2.0/LGPL-2.0)
- Firefox ESR (MPL-2.0)
- X.Org Server (MIT)
- And many more - see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)

## Acknowledgments

- [PRoot](https://github.com/proot-me/proot) - Rootless Linux userspace
- [Debian](https://www.debian.org/) - Universal operating system
- [XFCE](https://xfce.org/) - Lightweight desktop environment
- [Mozilla Firefox](https://www.mozilla.org/firefox/) - Web browser
- [Termux](https://termux.dev/) - Inspiration for Android Linux environment

## Support

- [Issues](https://github.com/yourusername/linux-cyberdeck/issues) - Bug reports and feature requests
- [Discussions](https://github.com/yourusername/linux-cyberdeck/discussions) - Questions and community

---

**Note**: This is an early development build. The Linux environment is large (~4GB) and requires significant storage. Performance on budget devices may vary.