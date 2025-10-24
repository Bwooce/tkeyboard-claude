#!/bin/bash
# T-Keyboard System Startup Script
# Ensures MCP server and input daemon use the same session ID

set -e

# Find Claude PID
CLAUDE_PID=$(ps aux | grep -E "^\w+\s+\d+.*claude\s*$" | grep -v grep | awk '{print $2}' | head -1)

if [ -z "$CLAUDE_PID" ]; then
    echo "❌ Error: Claude Code process not found"
    echo "   Make sure Claude Code is running"
    exit 1
fi

echo "✓ Found Claude process: PID $CLAUDE_PID"

# Generate session ID
SESSION_ID="tk-$(date +%s)-${CLAUDE_PID}"
echo "✓ Generated session ID: $SESSION_ID"

# Export for MCP server
export CLAUDE_SESSION_ID="$SESSION_ID"
export CLAUDE_PID="$CLAUDE_PID"

# Start MCP server
echo "✓ Starting MCP server..."
node build/tkeyboard-server.js > /tmp/tkeyboard-mcp.log 2>&1 &
MCP_PID=$!
echo "✓ MCP server started: PID $MCP_PID"

# Wait for server to be ready
sleep 2

# Verify server is running
if ! curl -s http://localhost:8081/status > /dev/null; then
    echo "❌ Error: MCP server failed to start"
    echo "   Check logs: tail /tmp/tkeyboard-mcp.log"
    exit 1
fi

echo "✓ MCP server is responding"

# Start input daemon
echo "✓ Starting input daemon..."
bash ../installation/tkeyboard-input-daemon.sh "$SESSION_ID" "$CLAUDE_PID" > /tmp/tkeyboard-daemon.log 2>&1 &
DAEMON_PID=$!
echo "✓ Input daemon started: PID $DAEMON_PID"

echo ""
echo "╔════════════════════════════════════════╗"
echo "║   T-Keyboard System Running            ║"
echo "╠════════════════════════════════════════╣"
echo "║   MCP Server:   PID $MCP_PID"
echo "║   Input Daemon: PID $DAEMON_PID"
echo "║   Session ID:   $SESSION_ID"
echo "║   Claude PID:   $CLAUDE_PID"
echo "╠════════════════════════════════════════╣"
echo "║   Logs:                                ║"
echo "║   • tail -f /tmp/tkeyboard-mcp.log     ║"
echo "║   • tail -f /tmp/tkeyboard-daemon.log  ║"
echo "╚════════════════════════════════════════╝"
echo ""
echo "Press Ctrl+C to stop (will kill both processes)"

# Trap Ctrl+C to clean up
trap "echo ''; echo 'Stopping...'; kill $MCP_PID $DAEMON_PID 2>/dev/null; exit 0" INT

# Keep script running
wait
