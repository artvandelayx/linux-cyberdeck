#!/bin/bash
# Local build script for Linux Cyberdeck APK
# Run this on a Linux machine (Ubuntu 22.04+/Debian 12+) with:
#   sudo apt-get install -y debootstrap qemu-user-static binfmt-support git curl
#   # Install Android SDK/NDK via Android Studio or command line tools
#   ./build_local.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
BUILD_DIR="$PROJECT_ROOT/build"
ROOTFS_DIR="$BUILD_DIR/rootfs-arm64"
ASSETS_DIR="$PROJECT_ROOT/app/src/main/assets"
JNI_LIBS_DIR="$PROJECT_ROOT/app/src/main/jniLibs/arm64-v8a"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${BLUE}[STEP]${NC} $1"; }

# Configuration
DEBIAN_VERSION="trixie"
DEBIAN_ARCH="arm64"
ANDROID_API=26
ANDROID_ABI="arm64-v8a"
ROOTFS_SIZE_GB=4

# Check dependencies
check_dependencies() {
    log_step "Checking dependencies..."
    
    local missing=()
    for cmd in debootstrap qemu-aarch64-static cmake git curl tar gzip; do
        if ! command -v "$cmd" &> /dev/null; then
            missing+=("$cmd")
        fi
    done
    
    if [ ${#missing[@]} -gt 0 ]; then
        log_error "Missing dependencies: ${missing[*]}"
        log_error "Install with: sudo apt-get install -y ${missing[*]}"
        exit 1
    fi
    
    # Check Android NDK
    if [ -z "${ANDROID_NDK_HOME:-}" ] && [ -z "${NDK_HOME:-}" ]; then
        log_warn "ANDROID_NDK_HOME not set. Trying common locations..."
        for path in \
            "$HOME/Android/Sdk/ndk"/* \
            "/opt/android-ndk"/* \
            "/usr/local/android-ndk"/*; do
            if [ -d "$path" ] && [ -f "$path/build/cmake/android.toolchain.cmake" ]; then
                export ANDROID_NDK_HOME="$path"
                log_info "Found NDK at: $ANDROID_NDK_HOME"
                break
            fi
        done
        
        if [ -z "${ANDROID_NDK_HOME:-}" ]; then
            log_error "Android NDK not found. Set ANDROID_NDK_HOME or install NDK r25+"
            exit 1
        fi
    fi
    
    log_info "All dependencies satisfied"
}

# Build Debian rootfs
build_rootfs() {
    log_step "Building Debian ${DEBIAN_VERSION} ${DEBIAN_ARCH} rootfs..."
    
    mkdir -p "$ROOTFS_DIR"
    
    log_info "Running debootstrap first stage..."
    sudo debootstrap --arch="$DEBIAN_ARCH" --foreign "$DEBIAN_VERSION" "$ROOTFS_DIR" http://deb.debian.org/debian
    
    log_info "Copying qemu static binary..."
    sudo cp /usr/bin/qemu-aarch64-static "$ROOTFS_DIR/usr/bin/"
    
    log_info "Running debootstrap second stage..."
    sudo chroot "$ROOTFS_DIR" /debootstrap/debootstrap --second-stage
    
    log_info "Configuring apt sources..."
    sudo tee "$ROOTFS_DIR/etc/apt/sources.list" << 'EOF'
deb http://deb.debian.org/debian trixie main contrib non-free non-free-firmware
deb http://deb.debian.org/debian trixie-updates main contrib non-free non-free-firmware
deb http://security.debian.org/debian-security trixie-security main contrib non-free non-free-firmware
EOF
    
    log_info "Installing base packages..."
    sudo chroot "$ROOTFS_DIR" apt-get update
    sudo chroot "$ROOTFS_DIR" apt-get install -y --no-install-recommends \
        bash coreutils util-linux procps iproute2 iputils-ping \
        nano vim-tiny curl wget ca-certificates \
        git python3 python3-pip python3-venv \
        openssh-client openssh-server \
        sudo locales tzdata \
        systemd-sysv \
        dbus
    
    log_info "Installing XFCE desktop..."
    sudo chroot "$ROOTFS_DIR" apt-get install -y --no-install-recommends \
        xfce4 xfce4-terminal thunar xfce4-panel xfdesktop4 xfwm4 xfce4-settings \
        xfce4-whiskermenu-plugin xfce4-pulseaudio-plugin \
        lightdm lightdm-gtk-greeter \
        dbus-x11 policykit-1 \
        xserver-xorg-core xserver-xorg-video-dummy xserver-xorg-video-fbdev \
        xvfb xwayland xauth x11-xserver-utils \
        mesa-utils glx-alternative-mesa
    
    log_info "Installing Firefox ESR..."
    sudo chroot "$ROOTFS_DIR" apt-get install -y --no-install-recommends firefox-esr
    
    log_info "Creating cyber user..."
    sudo chroot "$ROOTFS_DIR" useradd -m -s /bin/bash -G sudo,video,audio,render,plugdev cyber
    sudo chroot "$ROOTFS_DIR" sh -c 'echo "cyber:cyber" | chpasswd'
    sudo chroot "$ROOTFS_DIR" sh -c 'echo "root:root" | chpasswd'
    echo "cyber ALL=(ALL) NOPASSWD:ALL" | sudo tee "$ROOTFS_DIR/etc/sudoers.d/cyber"
    
    log_info "Configuring locale and timezone..."
    sudo chroot "$ROOTFS_DIR" sed -i 's/# en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen
    sudo chroot "$ROOTFS_DIR" locale-gen
    echo 'LANG="en_US.UTF-8"' | sudo tee "$ROOTFS_DIR/etc/default/locale"
    sudo ln -sf /usr/share/zoneinfo/UTC "$ROOTFS_DIR/etc/localtime"
    echo "UTC" | sudo tee "$ROOTFS_DIR/etc/timezone"
    
    log_info "Copying XFCE and LightDM configurations..."
    sudo mkdir -p "$ROOTFS_DIR/home/cyber/.config/xfce4/xfconf/xfce-perchannel-xml"
    sudo cp -r "$ASSETS_DIR/xfce-config/"* "$ROOTFS_DIR/home/cyber/.config/xfce4/"
    
    sudo mkdir -p "$ROOTFS_DIR/etc/lightdm"
    sudo cp -r "$ASSETS_DIR/lightdm/"* "$ROOTFS_DIR/etc/lightdm/"
    sudo chmod +x "$ROOTFS_DIR/etc/lightdm/"*.sh
    
    log_info "Fixing ownership..."
    sudo chroot "$ROOTFS_DIR" chown -R cyber:cyber /home/cyber
    
    log_info "Disabling unnecessary services..."
    sudo chroot "$ROOTFS_DIR" systemctl disable bluetooth cups ModemManager avahi-daemon 2>/dev/null || true
    
    log_info "Cleaning up..."
    sudo chroot "$ROOTFS_DIR" apt-get clean
    sudo rm -rf "$ROOTFS_DIR/var/lib/apt/lists/*"
    sudo rm -rf "$ROOTFS_DIR/usr/share/doc/*"
    sudo rm -rf "$ROOTFS_DIR/usr/share/man/*"
    sudo rm -rf "$ROOTFS_DIR/usr/share/locale/*"
    sudo find "$ROOTFS_DIR" -name "*.pyc" -delete
    sudo find "$ROOTFS_DIR" -name "__pycache__" -type d -exec rm -rf {} + 2>/dev/null || true
    
    log_info "Creating rootfs tarball..."
    mkdir -p "$ASSETS_DIR"
    cd "$BUILD_DIR"
    tar -czf "$ASSETS_DIR/rootfs-arm64.tgz" rootfs-arm64
    
    log_info "Rootfs tarball created: $ASSETS_DIR/rootfs-arm64.tgz"
    ls -lh "$ASSETS_DIR/rootfs-arm64.tgz"
}

# Build PRoot for Android ARM64
build_proot() {
    log_step "Cross-compiling PRoot for Android ARM64..."
    
    local PROOT_SRC="$BUILD_DIR/proot-src"
    local PROOT_BUILD="$BUILD_DIR/proot-build"
    local PROOT_INSTALL="$BUILD_DIR/proot-install"
    
    mkdir -p "$PROOT_BUILD" "$PROOT_INSTALL"
    
    if [ ! -d "$PROOT_SRC/.git" ]; then
        log_info "Cloning PRoot source..."
        git clone --depth 1 --branch v5.4.0 https://github.com/proot-me/proot.git "$PROOT_SRC"
        cd "$PROOT_SRC"
        git submodule update --init --recursive
    else
        log_info "PRoot source already exists, updating..."
        cd "$PROOT_SRC"
        git pull
        git submodule update --init --recursive
    fi
    
    log_info "Configuring PRoot for Android..."
    cd "$PROOT_BUILD"
    
    export ANDROID_NDK_HOME
    export TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64"
    export TARGET="aarch64-linux-android"
    export API=26
    
    export CC="$TOOLCHAIN/bin/${TARGET}${API}-clang"
    export CXX="$TOOLCHAIN/bin/${TARGET}${API}-clang++"
    export AR="$TOOLCHAIN/bin/llvm-ar"
    export STRIP="$TOOLCHAIN/bin/llvm-strip"
    export CFLAGS="--target=${TARGET}${API} -fPIE -pie"
    export LDFLAGS="--target=${TARGET}${API} -fPIE -pie -static-libstdc++"
    
    cmake "$PROOT_SRC" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$CC" \
        -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_AR="$AR" \
        -DCMAKE_STRIP="$STRIP" \
        -DCMAKE_C_FLAGS="$CFLAGS" \
        -DCMAKE_CXX_FLAGS="$CFLAGS" \
        -DCMAKE_EXE_LINKER_FLAGS="$LDFLAGS" \
        -DCMAKE_INSTALL_PREFIX="$PROOT_INSTALL" \
        -DWITH_SYSTEM_TALLOC=OFF \
        -DWITH_SYSTEM_LIBCap=OFF
    
    log_info "Building PRoot..."
    make -j$(nproc)
    make install
    
    log_info "Verifying PRoot binary..."
    file "$PROOT_INSTALL/bin/proot"
    "$PROOT_INSTALL/bin/proot" --version
    
    log_info "Copying PRoot to assets..."
    mkdir -p "$ASSETS_DIR"
    cp "$PROOT_INSTALL/bin/proot" "$ASSETS_DIR/proot"
    chmod +x "$ASSETS_DIR/proot"
}

# Build native libraries
build_native() {
    log_step "Building native libraries (JNI + Supervisor)..."
    
    cd "$PROJECT_ROOT/app"
    
    # Copy PRoot binary for CMake
    mkdir -p src/main/cpp/proot
    cp "$ASSETS_DIR/proot" src/main/cpp/proot/
    
    # Create CMakeLists.txt for PRoot prebuilt if not exists
    if [ ! -f src/main/cpp/proot/CMakeLists.txt ]; then
        cat > src/main/cpp/proot/CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.22.1)
project(proot-prebuilt)

add_library(proot STATIC IMPORTED)
set_target_properties(proot PROPERTIES
  IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/proot
  IMPORTED_LINK_INTERFACE_LIBRARIES ""
)
EOF
    fi
    
    log_info "Configuring CMake for Android..."
    cmake -B build/$ANDROID_ABI -S src/main/cpp \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$ANDROID_ABI" \
        -DANDROID_PLATFORM=android-$ANDROID_API \
        -DANDROID_NDK="$ANDROID_NDK_HOME" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_ANDROID_NDK_TOOLCHAIN_VERSION=clang
    
    log_info "Building native libraries..."
    cmake --build build/$ANDROID_ABI -j$(nproc) --config Release
    
    log_info "Copying native libraries to jniLibs..."
    mkdir -p "$JNI_LIBS_DIR"
    cp build/$ANDROID_ABI/jniLibs/$ANDROID_ABI/*.so "$JNI_LIBS_DIR/"
    
    log_info "Verifying native libraries..."
    ls -la "$JNI_LIBS_DIR/"
    file "$JNI_LIBS_DIR/liblinuxcyberdeck_jni.so"
}

# Assemble APK
assemble_apk() {
    log_step "Assembling APK..."
    
    cd "$PROJECT_ROOT"
    
    if [ ! -f gradlew ]; then
        log_warn "Gradle wrapper not found. Generating..."
        # Try to find gradle
        if command -v gradle &> /dev/null; then
            gradle wrapper
        else
            log_error "Gradle not found. Install Gradle or run 'gradle wrapper' in project root"
            exit 1
        fi
    fi
    
    chmod +x gradlew
    
    log_info "Building release APK..."
    ./gradlew :app:assembleRelease --no-daemon -Dorg.gradle.jvmargs="-Xmx4g"
    
    log_info "APK built successfully!"
    ls -lh app/build/outputs/apk/release/
}

# Main
main() {
    log_info "=========================================="
    log_info "Linux Cyberdeck APK Build Script"
    log_info "=========================================="
    
    check_dependencies
    
    # Parse arguments
    BUILD_ROOTFS=true
    BUILD_PROOT=true
    BUILD_NATIVE=true
    ASSEMBLE_APK=true
    
    for arg in "$@"; do
        case $arg in
            --no-rootfs) BUILD_ROOTFS=false ;;
            --no-proot) BUILD_PROOT=false ;;
            --no-native) BUILD_NATIVE=false ;;
            --no-apk) ASSEMBLE_APK=false ;;
            --help)
                echo "Usage: $0 [options]"
                echo "Options:"
                echo "  --no-rootfs   Skip rootfs build"
                echo "  --no-proot    Skip PRoot build"
                echo "  --no-native   Skip native libraries build"
                echo "  --no-apk      Skip APK assembly"
                echo "  --help        Show this help"
                exit 0
                ;;
        esac
    done
    
    if [ "$BUILD_ROOTFS" = true ]; then
        build_rootfs
    fi
    
    if [ "$BUILD_PROOT" = true ]; then
        build_proot
    fi
    
    if [ "$BUILD_NATIVE" = true ]; then
        build_native
    fi
    
    if [ "$ASSEMBLE_APK" = true ]; then
        assemble_apk
    fi
    
    log_info "=========================================="
    log_info "Build complete!"
    log_info "=========================================="
    log_info "APK location: $PROJECT_ROOT/app/build/outputs/apk/release/app-release.apk"
    log_info ""
    log_info "To install on device:"
    log_info "  adb install -r app/build/outputs/apk/release/app-release.apk"
}

main "$@"
