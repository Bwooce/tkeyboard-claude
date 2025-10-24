#!/bin/bash
# T-Keyboard System Stop Script
# Cleanly stops MCP server and input daemon

echo "Stopping T-Keyboard system..."

# Find and kill MCP server
MCP_PIDS=$(ps aux | grep "node build/tkeyboard-server.js" | grep -v grep | awk '{print $2}')
if [ -n "$MCP_PIDS" ]; then
    for pid in $MCP_PIDS; do
        echo "  Stopping MCP server (PID $pid)..."
        kill $pid 2>/dev/null
    done
fi

# Find and kill input daemon
DAEMON_PIDS=$(ps aux | grep "tkeyboard-input-daemon.sh" | grep -v grep | awk '{print $2}')
if [ -n "$DAEMON_PIDS" ]; then
    for pid in $DAEMON_PIDS; do
        echo "  Stopping input daemon (PID $pid)..."
        kill $pid 2>/dev/null
    done
fi

sleep 1

echo "✓ T-Keyboard system stopped"
