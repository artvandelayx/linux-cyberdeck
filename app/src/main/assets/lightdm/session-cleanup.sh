#!/bin/bash
# Session cleanup script for LightDM
# Runs after session ends

# Kill user D-Bus session bus
if [ -n "$DBUS_SESSION_BUS_PID" ]; then
    kill "$DBUS_SESSION_BUS_PID" 2>/dev/null || true
fi

# Kill user PulseAudio
if command -v pulseaudio >/dev/null 2>&1; then
    pulseaudio --kill 2>/dev/null || true
fi

# Clean up XDG runtime dir
if [ -n "$XDG_RUNTIME_DIR" ] && [ -d "$XDG_RUNTIME_DIR" ]; then
    rm -rf "$XDG_RUNTIME_DIR" 2>/dev/null || true
fi

# Clean up Xauthority
if [ -f "/tmp/.X1-auth" ]; then
    rm -f "/tmp/.X1-auth" 2>/dev/null || true
fi

exit 0