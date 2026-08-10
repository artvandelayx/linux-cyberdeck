#!/bin/bash
# Display setup script for LightDM
# Runs before X server starts

# Ensure X11 socket directory exists
mkdir -p /tmp/.X11-unix
chmod 1777 /tmp/.X11-unix

# Set up Xauthority
XAUTH_FILE="/tmp/.X1-auth"
if [ ! -f "$XAUTH_FILE" ]; then
    xauth -f "$XAUTH_FILE" generate :1 . trusted 2>/dev/null || true
fi

# Set up D-Bus
mkdir -p /run/dbus
dbus-uuidgen --ensure=/var/lib/dbus/machine-id 2>/dev/null || true

# Start system D-Bus if not running
if ! pgrep -x "dbus-daemon" >/dev/null; then
    dbus-daemon --system --fork 2>/dev/null || true
fi

exit 0