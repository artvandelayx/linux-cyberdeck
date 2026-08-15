#!/bin/bash
# Build script for Linux Cyberdeck APK
# This script builds the Debian rootfs and prepares the APK

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
ROOTFS_DIR="$BUILD_DIR/rootfs-arm64"
ASSETS_DIR="$PROJECT_ROOT/app/src/main/assets"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Check dependencies
check_dependencies() {
    log_info "Checking dependencies..."

    for cmd in debootstrap qemu-aarch64-static curl tar gzip; do
        if ! command -v "$cmd" &> /dev/null; then
            log_error "$cmd not found. Please install it."
            exit 1
        fi
    done

    # Check for Android NDK
    if [ -z "$ANDROID_NDK_HOME" ] && [ -z "$NDK_HOME" ]; then
        log_warn "ANDROID_NDK_HOME not set. NDK build may fail."
    fi
}

# Build Debian rootfs using debootstrap
build_rootfs() {
    log_info "Building Debian 13 (Trixie) ARM64 rootfs..."

    mkdir -p "$ROOTFS_DIR"

    # First stage: debootstrap --foreign
    log_info "Running debootstrap first stage..."
    debootstrap --arch=arm64 --foreign trixie "$ROOTFS_DIR" http://deb.debian.org/debian

    # Copy qemu for second stage
    cp /usr/bin/qemu-aarch64-static "$ROOTFS_DIR/usr/bin/"

    # Second stage: complete debootstrap inside chroot
    log_info "Running debootstrap second stage..."
    chroot "$ROOTFS_DIR" /debootstrap/debootstrap --second-stage

    # Configure apt sources
    cat > "$ROOTFS_DIR/etc/apt/sources.list" << 'EOF'
deb http://deb.debian.org/debian trixie main contrib non-free non-free-firmware
deb http://deb.debian.org/debian trixie-updates main contrib non-free non-free-firmware
deb http://security.debian.org/debian-security trixie-security main contrib non-free non-free-firmware
EOF

    # Update and install base packages
    log_info "Installing base packages..."
    chroot "$ROOTFS_DIR" apt-get update
    chroot "$ROOTFS_DIR" apt-get install -y \
        bash coreutils util-linux procps iproute2 iputils-ping \
        nano vim-tiny curl wget ca-certificates \
        git python3 python3-pip python3-venv \
        openssh-client openssh-server \
        sudo locales tzdata

    # Install XFCE desktop
    log_info "Installing XFCE desktop..."
    chroot "$ROOTFS_DIR" apt-get install -y \
        xfce4 xfce4-terminal thunar xfce4-panel xfdesktop4 xfwm4 xfce4-settings \
        xfce4-whiskermenu-plugin xfce4-pulseaudio-plugin \
        lightdm lightdm-gtk-greeter \
        dbus-x11 policykit-1

    # Install Firefox (ESR for stability)
    log_info "Installing Firefox..."
    chroot "$ROOTFS_DIR" apt-get install -y firefox-esr

    # Install X11 server components
    log_info "Installing X11 server..."
    chroot "$ROOTFS_DIR" apt-get install -y \
        xserver-xorg-core xserver-xorg-video-dummy xserver-xorg-video-fbdev \
        xvfb xauth x11-xserver-utils x11vnc novnc websockify

    # Create cyber user
    log_info "Creating cyber user..."
    chroot "$ROOTFS_DIR" useradd -m -s /bin/bash -G sudo,video,audio cyber
    chroot "$ROOTFS_DIR" sh -c 'echo "cyber:cyber" | chpasswd'
    chroot "$ROOTFS_DIR" sh -c 'echo "root:root" | chpasswd'

    # Configure sudo for cyber user
    echo "cyber ALL=(ALL) NOPASSWD:ALL" > "$ROOTFS_DIR/etc/sudoers.d/cyber"

    # Configure locale
    chroot "$ROOTFS_DIR" sed -i 's/# en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen
    chroot "$ROOTFS_DIR" locale-gen
    echo 'LANG="en_US.UTF-8"' > "$ROOTFS_DIR/etc/default/locale"

    # Configure timezone
    ln -sf /usr/share/zoneinfo/UTC "$ROOTFS_DIR/etc/localtime"
    echo "UTC" > "$ROOTFS_DIR/etc/timezone"

    # Configure XFCE for touchscreen
    log_info "Configuring XFCE for touchscreen..."
    mkdir -p "$ROOTFS_DIR/home/cyber/.config/xfce4/xfconf/xfce-perchannel-xml"

    # Copy XFCE and LightDM configurations from assets
    log_info "Copying XFCE and LightDM configurations..."

    # XFCE panel config
    mkdir -p "$ROOTFS_DIR/home/cyber/.config/xfce4/xfconf/xfce-perchannel-xml"
    cp "$PROJECT_ROOT/app/src/main/assets/xfce-config/xfce4-panel/xfce4-panel.xml" \
       "$ROOTFS_DIR/home/cyber/.config/xfce4/xfconf/xfce-perchannel-xml/"

    # XFCE terminal config
    mkdir -p "$ROOTFS_DIR/home/cyber/.config/xfce4/terminal"
    cp "$PROJECT_ROOT/app/src/main/assets/xfce-config/xfce4-terminal/terminalrc" \
       "$ROOTFS_DIR/home/cyber/.config/xfce4/terminal/"

    # XFWM4 config
    cp "$PROJECT_ROOT/app/src/main/assets/xfce-config/xfwm4/xfwm4.xml" \
       "$ROOTFS_DIR/home/cyber/.config/xfce4/xfconf/xfce-perchannel-xml/"

    # XSettings config
    cp "$PROJECT_ROOT/app/src/main/assets/xfce-config/xsettings/xsettings.xml" \
       "$ROOTFS_DIR/home/cyber/.config/xfce4/xfconf/xfce-perchannel-xml/"

    # LightDM config
    mkdir -p "$ROOTFS_DIR/etc/lightdm"
    cp "$PROJECT_ROOT/app/src/main/assets/lightdm/lightdm.conf" \
       "$ROOTFS_DIR/etc/lightdm/"
    cp "$PROJECT_ROOT/app/src/main/assets/lightdm/lightdm-gtk-greeter.conf" \
       "$ROOTFS_DIR/etc/lightdm/"
    cp "$PROJECT_ROOT/app/src/main/assets/lightdm/xsession-wrapper" \
       "$ROOTFS_DIR/etc/lightdm/"
    cp "$PROJECT_ROOT/app/src/main/assets/lightdm/display-setup.sh" \
       "$ROOTFS_DIR/etc/lightdm/"
    cp "$PROJECT_ROOT/app/src/main/assets/lightdm/session-setup.sh" \
       "$ROOTFS_DIR/etc/lightdm/"
    cp "$PROJECT_ROOT/app/src/main/assets/lightdm/session-cleanup.sh" \
       "$ROOTFS_DIR/etc/lightdm/"
    chmod +x "$ROOTFS_DIR/etc/lightdm/"*.sh

    # Fix ownership of configs
    chroot "$ROOTFS_DIR" chown -R cyber:cyber /home/cyber

    # Configure XFCE session startup
    mkdir -p "$ROOTFS_DIR/home/cyber/.config/autostart"

    # Disable unnecessary services for performance
    chroot "$ROOTFS_DIR" systemctl disable --now \
        bluetooth cups ModemManager avahi-daemon 2>/dev/null || true

    # Clean up apt cache
    chroot "$ROOTFS_DIR" apt-get clean
    rm -rf "$ROOTFS_DIR/var/lib/apt/lists/*"

    log_info "Rootfs build complete at $ROOTFS_DIR"
}

# Create rootfs tarball for APK assets
create_rootfs_tarball() {
    log_info "Creating rootfs tarball..."

    mkdir -p "$ASSETS_DIR"

    # Create compressed tarball
    tar -czf "$ASSETS_DIR/rootfs-arm64.tar.gz" -C "$BUILD_DIR" rootfs-arm64

    log_info "Rootfs tarball created: $ASSETS_DIR/rootfs-arm64.tar.gz"
    ls -lh "$ASSETS_DIR/rootfs-arm64.tar.gz"
}

# Build native libraries
build_native() {
    log_info "Building native libraries..."

    cd "$PROJECT_ROOT/app"

    # This would normally use gradle to build the NDK components
    # ./gradlew :app:externalNativeBuildDebug

    log_warn "Native build should be done via Android Studio/Gradle"
}

# Main
main() {
    log_info "Starting Linux Cyberdeck build..."

    check_dependencies
    build_rootfs
    create_rootfs_tarball
    build_native

    log_info "Build complete!"
    log_info "Next steps:"
    log_info "1. Open project in Android Studio"
    log_info "2. Build APK (Build > Build Bundle(s) / APK(s) > Build APK(s))"
    log_info "3. Install on device"
}

main "$@"
