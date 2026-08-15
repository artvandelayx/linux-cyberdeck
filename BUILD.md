# Linux Cyberdeck - Build Instructions

This project requires building several large components (~4 GB rootfs + native libraries) that cannot be built on Windows directly. You have two options:

## Option 1: GitHub Actions (Recommended - No Local Setup)

1. **Push this repository to GitHub**
2. **Enable Actions** in the repository settings
3. **Trigger a build**:
   - Go to Actions tab → "Build Linux Cyberdeck APK" → "Run workflow"
   - Or push a tag: `git tag v1.0.0 && git push origin v1.0.0`
4. **Download the APK** from:
   - Workflow artifacts (for any build)
   - GitHub Releases (for tagged builds)

The workflow (`.github/workflows/build-apk.yml`) automatically:
- Builds Debian 13 (Trixie) ARM64 rootfs via debootstrap (~3-4 GB)
- Cross-compiles PRoot v5.4.0 for Android ARM64
- Builds native JNI libraries with FUSE storage bridge
- Assembles the final signed APK

## Option 2: Local Linux Build

### Prerequisites (Ubuntu 22.04+ / Debian 12+)

```bash
# Install build dependencies
sudo apt-get update
sudo apt-get install -y \
    debootstrap \
    qemu-user-static \
    binfmt-support \
    git \
    cmake \
    build-essential \
    pkg-config \
    libtalloc-dev \
    libcap-dev \
    libseccomp-dev \
    curl \
    tar \
    gzip \
    openjdk-17-jdk

# Install Android SDK/NDK (via Android Studio or command line)
# Android Studio: Tools → SDK Manager → SDK Tools → NDK (r25+)
# Or command line:
#   sdkmanager "ndk;27.0.12077973" "cmake;3.22.1" "build-tools;34.0.0" "platforms;android-34"

export ANDROID_NDK_HOME="$HOME/Android/Sdk/ndk/27.0.12077973"
export ANDROID_HOME="$HOME/Android/Sdk"
```

### Build

```bash
# Make script executable
chmod +x build_local.sh

# Full build (takes 30-60 minutes, downloads ~2 GB)
./build_local.sh

# Or build individual components
./build_local.sh --no-rootfs    # Skip rootfs (use existing)
./build_local.sh --no-proot     # Skip PRoot (use existing)
./build_local.sh --no-native    # Skip native libs (use existing)
./build_local.sh --no-apk       # Only build components, not APK
```

### Output

- **APK**: `app/build/outputs/apk/release/app-release.apk`
- **Rootfs tarball**: `app/src/main/assets/rootfs-arm64.tgz` (~3-4 GB)
- **PRoot binary**: `app/src/main/assets/proot`
- **Native libs**: `app/src/main/jniLibs/arm64-v8a/liblinuxcyberdeck_jni.so`

## Device Requirements

- **Android 14+** (API 34)
- **ARM64 (aarch64)** device
- **4+ GB RAM** (8 GB recommended)
- **8+ GB free storage** (for rootfs extraction)
- **Root NOT required** (uses PRoot + SAF)

## Installation

```bash
# Via ADB
adb install -r app/build/outputs/apk/release/app-release.apk

# Or transfer APK to device and install via file manager
```

## First Run

1. Open app → "START LINUX"
2. Grant storage permission when prompted (for `/mnt/shared`)
3. Wait for rootfs extraction (~5-10 minutes on first run)
4. Linux desktop (XFCE) will start on display :1

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│ Android App (Kotlin)                                    │
│  ├── MainActivity + ViewModels                          │
│  ├── StorageBridgeManager (SAF ↔ FUSE)                  │
│  └── LinuxSessionManager (lifecycle)                    │
├─────────────────────────────────────────────────────────┤
│ JNI Bridge (liblinuxcyberdeck_jni.so)                   │
├─────────────────────────────────────────────────────────┤
│ Native Supervisor (linux_supervisor)                    │
│  ├── Process Monitor (Xvfb, Xwayland, PRoot, LightDM)  │
│  ├── Storage Bridge (FUSE ↔ SAF via JNI)                │
│  └── X11 Manager                                        │
├─────────────────────────────────────────────────────────┤
│ PRoot (proot binary) - Runs Linux ELF binaries          │
├─────────────────────────────────────────────────────────┤
│ Debian 13 Rootfs (extracted to app files dir)           │
│  ├── Xvfb + Xwayland (Display :1)                       │
│  ├── LightDM → Auto-login cyber → XFCE                  │
│  ├── Firefox ESR                                         │
│  └── Storage mounts: /mnt/shared, /mnt/sdcard           │
└─────────────────────────────────────────────────────────┘
```

## Troubleshooting

| Issue | Solution |
|-------|----------|
| "Insufficient storage" | Need 8+ GB free; rootfs extracts to `/data/data/com.linuxcyberdeck/files/linux/rootfs` |
| PRoot fails | Check `logcat -s LinuxCyberdeck`; ensure `proot` binary is executable |
| X11 not starting | Check `/data/data/com.linuxcyberdeck/files/linux/logs/xvfb.log` |
| Storage not mounting | Grant "All files access" in Android settings; re-select folder in app |
| Build fails | Ensure NDK r25+ and CMake 3.22+; check `externalNativeBuild` logs |

## License

GPL-3.0 - See LICENSE and THIRD_PARTY_LICENSES.md for details.
