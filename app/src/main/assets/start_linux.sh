#!/bin/bash
# X11 + XFCE startup script for Linux Cyberdeck
# This runs INSIDE the PRoot environment

set -e

# Configuration
DISPLAY_NUM=1
XVFB_SCREEN="1920x1080x24"
XAUTH_FILE="/tmp/.X${DISPLAY_NUM}-auth"
LOG_DIR="/var/log/linux-cyberdeck"

# Ensure log directory
mkdir -p "$LOG_DIR"

# Logging function
log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a "$LOG_DIR/startup.log"
}

# Cleanup function
cleanup() {
    log "Cleaning up..."
    pkill -f "Xvfb :$DISPLAY_NUM" 2>/dev/null || true
    pkill -f "Xwayland :$DISPLAY_NUM" 2>/dev/null || true
    pkill -f "lightdm" 2>/dev/null || true
    pkill -f "xfce4-session" 2>/dev/null || true
    rm -f "$XAUTH_FILE"
    rm -f /tmp/.X${DISPLAY_NUM}-lock
}
trap cleanup EXIT INT TERM

log "=== Linux Cyberdeck Startup ==="
log "Display: :$DISPLAY_NUM"
log "Screen: $XVFB_SCREEN"

# Generate Xauthority cookie
log "Generating Xauthority..."
COOKIE="$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')"
xauth -f "$XAUTH_FILE" add ":$DISPLAY_NUM" . "$COOKIE" 2>>"$LOG_DIR/xauth.log"
export XAUTHORITY="$XAUTH_FILE"

# Start Xvfb
log "Starting Xvfb..."
Xvfb ":$DISPLAY_NUM" -screen 0 "$XVFB_SCREEN" -nolisten tcp -auth "$XAUTH_FILE" -noreset \
    >>"$LOG_DIR/xvfb.log" 2>&1 &
XVFB_PID=$!
log "Xvfb PID: $XVFB_PID"

# Wait for Xvfb to be ready
sleep 2

# Test Xvfb
if ! xset -display ":$DISPLAY_NUM" q >/dev/null 2>&1; then
    log "ERROR: Xvfb failed to start"
    exit 1
fi
log "Xvfb ready"

log "Starting loopback display bridge..."
x11vnc -display ":$DISPLAY_NUM" -localhost -forever -shared -nopw -noshm -rfbport 5901 \
    >>"$LOG_DIR/x11vnc.log" 2>&1 &
websockify --web /usr/share/novnc 127.0.0.1:6080 127.0.0.1:5901 \
    >>"$LOG_DIR/novnc.log" 2>&1 &

# Start D-Bus
log "Starting D-Bus..."
mkdir -p /run/dbus
dbus-daemon --system --fork >>"$LOG_DIR/dbus.log" 2>&1
dbus-daemon --session --fork >>"$LOG_DIR/dbus.log" 2>&1
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/dbus/session_bus_socket"

# Start PulseAudio (optional, for audio)
if command -v pulseaudio >/dev/null 2>&1; then
    log "Starting PulseAudio..."
    pulseaudio --start --log-target=syslog >>"$LOG_DIR/pulseaudio.log" 2>&1 || true
fi

# Set up environment for XFCE
export DISPLAY=":$DISPLAY_NUM"
export XAUTHORITY="$XAUTH_FILE"
export XDG_RUNTIME_DIR="/run/user/1000"
export XDG_SESSION_TYPE="x11"
export XDG_CURRENT_DESKTOP="XFCE"
export XDG_SESSION_DESKTOP="xfce"
export XDG_SESSION_CLASS="user"
export DESKTOP_SESSION="xfce"
export GTK_THEME="Adwaita"
export QT_QPA_PLATFORM="xcb"

mkdir -p "$XDG_RUNTIME_DIR"
chown cyber:cyber "$XDG_RUNTIME_DIR"

# Start the desktop directly as the unprivileged account. Display managers rely
# on kernel/PAM facilities that are intentionally unavailable inside PRoot.
log "Starting XFCE as cyber..."
exec su -s /bin/bash cyber -c \
    'export DISPLAY=:1 XAUTHORITY=/tmp/.X1-auth HOME=/home/cyber USER=cyber LOGNAME=cyber XDG_RUNTIME_DIR=/run/user/1000; exec dbus-launch --exit-with-session startxfce4' \
    >>"$LOG_DIR/xfce.log" 2>&1
