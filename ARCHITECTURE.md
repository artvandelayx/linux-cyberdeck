# Architecture

## Overview

Linux Cyberdeck is a self-contained Android application that provides a complete Debian Linux userspace with graphical desktop environment, running entirely in userspace without root access.

## Core Components

### 1. Android Application Layer (Kotlin)

**MainActivity** - Primary UI with state-driven interface:
- Session state display (stopped/installing/running/error)
- Main action button (START LINUX / OPEN DESKTOP)
- Quick action buttons (Terminal, Firefox, Files)
- Control buttons (Restart, Stop, Settings)
- Progress indication during install/startup

**LinuxSessionManager (ViewModel)** - Session lifecycle management:
- State machine: NOT_INSTALLED → INSTALLING → INSTALLED → STARTING → RUNNING
- Error handling with recovery options
- LiveData observables for reactive UI updates
- Coordination with native layer via JNI

**LinuxSessionService (Foreground Service)** - Background session persistence:
- Keeps Linux processes alive when app is backgrounded
- Handles BOOT_COMPLETED for auto-start
- Notification with stop/restart actions
- Partial wake lock to prevent CPU throttling

**StorageBridgeManager** - Android ↔ Linux storage integration:
- Uses Storage Access Framework (ACTION_OPEN_DOCUMENT_TREE)
- Persistable URI permissions for cross-session access
- Coordinates with native FUSE bridge

### 2. Native Layer (C/C++ via NDK)

#### JNI Bridge (`jni_bridge.c`)
- Translates Kotlin calls to native C functions
- Converts data structures between Java/Kotlin and C
- Manages string encoding (UTF-8 ↔ UTF-16)
- Creates Java objects for status/diagnostics

#### Linux Session Supervisor (`linux_session.c`)
- Central process orchestration
- State machine implementation
- Progress tracking and error reporting
- Monitor thread for component health checks

#### Process Monitor (`process_monitor.c`)
- PID registry for all managed processes
- Health checks via `kill(pid, 0)`
- Graceful shutdown with SIGTERM/SIGKILL
- Timeout-based process waiting

#### X11 Manager (`x11_manager.c`)
- Starts Xvfb (virtual framebuffer) on display :1
- Starts Xwayland for X11-on-Wayland compatibility
- Generates and manages Xauthority cookies
- Tests X connection readiness

#### Storage Bridge (`storage_bridge.c`)
- SAF document-tree bridge. Android does not expose `/dev/fuse` to ordinary apps,
  so selected trees use `ContentResolver` rather than fake POSIX mounts.
- Mounts Android SAF URIs as Linux directories
- Translates POSIX calls → ContentResolver operations
- Manages multiple simultaneous mounts

### 3. Linux Userspace (Debian 13 ARM64)

#### Rootfs Structure
```
/data/data/com.linuxcyberdeck/files/linux/
├── rootfs/          # Debian root filesystem (read-only base + writable overlay)
│   ├── bin/
│   ├── etc/
│   ├── home/cyber/  # User home directory
│   ├── lib/
│   ├── usr/
│   └── var/
├── home/            # Bind mount for /home/cyber persistence
├── logs/            # Component logs (x11.log, linux.log, xfce.log, etc.)
└── tmp/             # Temporary files
```

#### Key Configurations

**User: cyber** (UID 1000)
- Member of: sudo, video, audio, plugdev
- Password: "cyber" (change on first login)
- Auto-login via LightDM

**XFCE Touch Optimizations**
- Panel size: 48px (default ~27px)
- Icon size: 32px (default ~16px)
- Font: Sans 12 (default ~10)
- Terminal font: Monospace 14
- Window title font: Sans Bold 11
- Whisker menu icon size: 32px

**X11 Display: :1**
- Xvfb: 1920x1080x24 virtual screen
- Xwayland: Rootless mode for better integration
- XAUTHORITY: /tmp/.X1-auth

**Services**
- LightDM (display manager) → auto-login cyber → XFCE session
- D-Bus system/session buses
- PulseAudio (via module-native-protocol-unix)
- SSH server (disabled by default, bind to localhost)

#### Package List (Minimum)
```
Base: bash coreutils util-linux procps iproute2 iputils-ping
Editor: nano vim-tiny
Network: curl wget ca-certificates openssh-client openssh-server
Dev: git python3 python3-pip python3-venv
Desktop: xfce4 xfce4-terminal thunar xfce4-panel xfdesktop4 xfwm4 xfce4-settings
Display: xvfb xwayland xauth xserver-xorg-core
Browser: firefox-esr
```

## Process Flow

### First Launch (Installation)
```
User taps START LINUX
        │
        ▼
LinuxSessionManager.installRootfs()
        │
        ▼
Native: lc_install_rootfs()
        │
        ├─► Verify storage space (need ~4GB)
        │
        ├─► Download rootfs tarball (streaming)
        │
        ├─► Extract to /data/data/.../linux/rootfs/
        │
        ├─► Run debootstrap second-stage in PRoot
        │
        ├─► Install packages via apt
        │
        ├─► Configure user, XFCE, auto-login
        │
        └─► State = INSTALLED
```

### Normal Startup
```
User taps START LINUX (state=INSTALLED)
        │
        ▼
LinuxSessionManager.startLinux()
        │
        ▼
Native: lc_start_linux_session()
        │
        ├─► State = STARTING, progress = 0.1
        │
        ├─► Start Xvfb :1 + Xwayland
        │    └─► progress = 0.3
        │
        ├─► Start PRoot with Debian rootfs
        │    ├─► Mount /proc, /sys, /dev, /tmp
        │    ├─► Bind mount /home/cyber
        │    ├─► Bind mount storage bridges
        │    ├─► Start D-Bus
        │    └─► progress = 0.5
        │
        ├─► Inside PRoot: start LightDM
        │    └─► Auto-login cyber → startxfce4
        │    └─► progress = 0.8
        │
        └─► State = RUNNING, progress = 1.0
```

### Recovery Loop (Monitor Thread)
```
Every 5 seconds:
        │
        ▼
Check Xvfb/Xwayland PIDs alive?
        │
        ├─ No → Restart X11 → Restart XFCE
        │
        ▼
Check PRoot PID alive?
        │
        ├─ No → Restart PRoot → Restart X11 → Restart XFCE
        │
        ▼
Check XFCE PID alive?
        │
        ├─ No → Restart XFCE inside PRoot
        │
        ▼
All healthy → Continue
```

## Storage Bridge Architecture

```
Android App                          Linux (PRoot)
┌──────────────────┐                ┌──────────────────┐
│ StorageBridgeMgr │                │  FUSE Daemon     │
│                  │   JNI + IPC    │                  │
│ ACTION_OPEN_     │◄──────────────►│  /mnt/shared     │
│ DOCUMENT_TREE    │                │  /mnt/sdcard     │
│                  │   ContentResolver calls  │
│ Persistable URI  │                │  (POSIX API)     │
│ Permission       │                │                  │
└──────────────────┘                └──────────────────┘
```

**Implementation Options:**
1. **FUSE daemon in native layer** - Translates POSIX → ContentResolver (preferred)
2. **bindfs + Android binder** - Mount via binder IPC
3. **Simple bind mount** - Only works for accessible paths (limited)

## Security Model

- **No root access** - All components run as unprivileged Android app UID
- **PRoot isolation** - ptrace-based syscall interception for filesystem virtualization
- **Android permissions** - Only FOREGROUND_SERVICE, STORAGE (via SAF), INTERNET
- **SSH server** - Binds to 127.0.0.1 by default, not exposed to network
- **X11** - `-nolisten tcp` prevents network connections
- **SELinux** - Runs in untrusted_app domain, no policy modifications

## Performance Considerations

- **Memory**: Target 2-3GB RAM usage (Xvfb + Xwayland + XFCE + Firefox + Debian)
- **CPU**: PRoot adds ~5-15% overhead for syscall interception
- **Storage**: ~4GB for rootfs + user data
- **Battery**: Foreground service + X11 + desktop = significant drain; optimize with:
  - Dim screen when not in use
  - Stop Linux when not needed
  - Disable auto-start unless needed

## Error Handling

Each component has:
- Structured error codes (LC_RESULT_*)
- Human-readable error messages
- Automatic recovery attempts (max 3 retries)
- Diagnostic logging to separate log files
- UI error banner with Restart/View Log actions

## Future Extensibility

- **Plugin system** - Additional Linux packages via APK expansion files
- **Multiple distributions** - Alpine, Arch, Fedora rootfs options
- **Container support** - Podman/Docker inside PRoot
- **GPU acceleration** - VirGL/VirGLRenderer for OpenGL
- **Multi-display** - External monitor support via Presentation API
