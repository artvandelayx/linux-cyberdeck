#!/bin/bash
# Session setup script for LightDM
# Runs after user logs in, before session starts

USER_HOME="$HOME"
USER_NAME="$(whoami)"

# Set up user directories
mkdir -p "$USER_HOME/.config"
mkdir -p "$USER_HOME/.cache"
mkdir -p "$USER_HOME/.local/share"
mkdir -p "$USER_HOME/.local/state"

# Set up XDG runtime dir
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
chown "$(id -u):$(id -g)" "$XDG_RUNTIME_DIR"

# Start user D-Bus session bus
if [ -z "$DBUS_SESSION_BUS_ADDRESS" ]; then
    export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
fi

# Ensure D-Bus is running
dbus-daemon --session --fork --address="$DBUS_SESSION_BUS_ADDRESS" 2>/dev/null || true

# Set up PulseAudio for user
if command -v pulseaudio >/dev/null 2>&1; then
    pulseaudio --start --log-target=syslog 2>/dev/null || true
fi

# Set up GTK modules
export GTK_MODULES="gail:atk-bridge"

# Set up accessibility
export QT_ACCESSIBILITY=1

# Set up environment for XFCE
export XDG_CURRENT_DESKTOP="XFCE"
export XDG_SESSION_DESKTOP="xfce"
export XDG_SESSION_TYPE="x11"
export DESKTOP_SESSION="xfce"

exit 0