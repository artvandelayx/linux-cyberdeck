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
xauth -f "$XAUTH_FILE" generate ":$DISPLAY_NUM" . trusted 2>>"$LOG_DIR/xauth.log"
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

# Start Xwayland
log "Starting Xwayland..."
Xwayland ":$DISPLAY_NUM" -rootless -auth "$XAUTH_FILE" -nolisten tcp \
    >>"$LOG_DIR/xwayland.log" 2>&1 &
XWAYLAND_PID=$!
log "Xwayland PID: $XWAYLAND_PID"

# Wait for Xwayland
sleep 2

# Test X connection
if ! xset -display ":$DISPLAY_NUM" q >/dev/null 2>&1; then
    log "ERROR: Xwayland failed to start"
    kill $XVFB_PID 2>/dev/null
    exit 1
fi
log "Xwayland ready"

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

# Start LightDM (which will auto-login cyber and start XFCE)
log "Starting LightDM..."
exec lightdm --test-mode=0 >>"$LOG_DIR/lightdm.log" 2>&1