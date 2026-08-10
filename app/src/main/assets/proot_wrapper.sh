#!/bin/bash
# PRoot wrapper for Android
# This script sets up the PRoot environment and executes commands inside Debian

set -e

# Paths (will be replaced at runtime by the native layer)
ROOTFS_DIR="${ROOTFS_DIR:-/data/data/com.linuxcyberdeck/files/linux/rootfs}"
HOME_DIR="${HOME_DIR:-/data/data/com.linuxcyberdeck/files/linux/home/cyber}"
TMP_DIR="${TMP_DIR:-/data/data/com.linuxcyberdeck/files/linux/tmp}"
LOGS_DIR="${LOGS_DIR:-/data/data/com.linuxcyberdeck/files/linux/logs}"

# PRoot binary (bundled in assets, extracted to app files dir)
PROOT_BIN="${PROOT_BIN:-/data/data/com.linuxcyberdeck/files/proot}"

# Display configuration
export DISPLAY=":1"
export XAUTHORITY="/tmp/.X1-auth"

# Ensure directories exist
mkdir -p "$HOME_DIR" "$TMP_DIR" "$LOGS_DIR"

# PRoot arguments
PROOT_ARGS=(
    "--rootfs=$ROOTFS_DIR"
    "--root-id"
    "--kill-on-exit"
    "--link2symlink"
    "--bind=/proc:/proc"
    "--bind=/sys:/sys"
    "--bind=/dev:/dev"
    "--bind=/dev/pts:/dev/pts"
    "--bind=/tmp:/tmp"
    "--bind=$HOME_DIR:/home/cyber"
    "--bind=$TMP_DIR:/tmp"
    "--bind=/system/bin:/system/bin"
    "--bind=/system/lib64:/system/lib64"
    "--bind=/vendor/bin:/vendor/bin"
    "--bind=/vendor/lib64:/vendor/lib64"
    "--cwd=/home/cyber"
    "--env=HOME=/home/cyber"
    "--env=USER=cyber"
    "--env=LOGNAME=cyber"
    "--env=SHELL=/bin/bash"
    "--env=TERM=xterm-256color"
    "--env=DISPLAY=:1"
    "--env=XAUTHORITY=/tmp/.X1-auth"
    "--env=LANG=en_US.UTF-8"
    "--env=LC_ALL=en_US.UTF-8"
    "--env=PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
)

# Add storage bridges if mounted
if [ -d "/mnt/shared" ]; then
    PROOT_ARGS+=("--bind=/mnt/shared:/mnt/shared")
fi

if [ -d "/mnt/sdcard" ]; then
    PROOT_ARGS+=("--bind=/mnt/sdcard:/mnt/sdcard")
fi

# Execute command
exec "$PROOT_BIN" "${PROOT_ARGS[@]}" "$@"