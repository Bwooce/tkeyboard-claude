#!/bin/bash
# Start input daemon (for use when Claude Code manages MCP server)
# Auto-discovers session ID from running MCP server

echo "Checking for running MCP server..."

# Wait for server to be available
for i in {1..10}; do
    if curl -s http://localhost:8081/status > /dev/null 2>&1; then
        break
    fi
    echo "Waiting for MCP server... ($i/10)"
    sleep 1
done

# Get session info from server
STATUS=$(curl -s http://localhost:8081/status)
if [ -z "$STATUS" ]; then
    echo "❌ Error: MCP server not responding"
    echo "   Make sure Claude Code is running and MCP server is configured"
    exit 1
fi

SESSION_ID=$(echo "$STATUS" | jq -r '.sessionId')
CLAUDE_PID=$(echo "$STATUS" | jq -r '.claudePid')

echo "✓ Found MCP server"
echo "  Session ID: $SESSION_ID"
echo "  Claude PID: $CLAUDE_PID"

# Start input daemon
echo "✓ Starting input daemon..."
bash ../installation/tkeyboard-input-daemon.sh "$SESSION_ID" "$CLAUDE_PID" > /tmp/tkeyboard-daemon.log 2>&1 &
DAEMON_PID=$!

echo "✓ Input daemon started: PID $DAEMON_PID"
echo ""
echo "Logs: tail -f /tmp/tkeyboard-daemon.log"
