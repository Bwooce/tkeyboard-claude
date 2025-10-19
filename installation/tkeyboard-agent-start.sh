#!/bin/bash
# T-Keyboard Agent Startup Script
# Starts bridge server and stop daemon with session binding

# Detect Claude process and TTY
# Find the actual Claude process (parent of bash)
CLAUDE_PID=$(ps -o ppid= -p $$ | tr -d ' ')

# Get TTY from Claude process
TTY_NAME=$(ps -o tty= -p "$CLAUDE_PID" | tr -d ' ')
if [ "$TTY_NAME" == "??" ] || [ -z "$TTY_NAME" ]; then
    # Fallback: search for claude process with TTY
    CLAUDE_INFO=$(ps aux | grep -E "^\s*$USER\s+[0-9]+.*claude\s*$" | grep -v grep | grep -v '??' | head -1)
    if [ ! -z "$CLAUDE_INFO" ]; then
        CLAUDE_PID=$(echo "$CLAUDE_INFO" | awk '{print $2}')
        TTY_NAME=$(echo "$CLAUDE_INFO" | awk '{print $7}')
    fi
fi

# Convert TTY name to full path (e.g., s005 -> /dev/ttys005)
if [[ "$TTY_NAME" =~ ^s[0-9]+ ]]; then
    TTY_PATH="/dev/tty$TTY_NAME"
else
    TTY_PATH="/dev/$TTY_NAME"
fi

# Generate unique session ID
SESSION_ID="tk-$(date +%s)-${CLAUDE_PID}"

echo "╔════════════════════════════════════════╗"
echo "║   T-Keyboard Session-Bound Startup     ║"
echo "╠════════════════════════════════════════╣"
echo "║   Session ID: $SESSION_ID"
echo "║   Claude PID: $CLAUDE_PID"
echo "║   TTY:        $TTY_PATH"
echo "╚════════════════════════════════════════╝"
echo ""

# Kill any existing bridge servers
echo "[Startup] Killing existing bridge servers..."
lsof -ti:8080,8081 | xargs kill -9 2>/dev/null
sleep 1

# Start bridge server with session environment
echo "[Startup] Starting bridge server with SESSION_ID=$SESSION_ID"
cd /Users/bruce/Documents/Arduino/tkeyboard-claude/bridge-server
CLAUDE_SESSION_ID="$SESSION_ID" CLAUDE_PID="$CLAUDE_PID" node agent-bridge.js > /tmp/tkeyboard-bridge-$SESSION_ID.log 2>&1 &
BRIDGE_PID=$!

echo "[Startup] Bridge server started (PID=$BRIDGE_PID)"

# Wait for bridge server to be ready
echo "[Startup] Waiting for bridge server..."
for i in {1..10}; do
    if curl -s http://localhost:8081/status > /dev/null 2>&1; then
        echo "[Startup] Bridge server ready!"
        break
    fi
    sleep 0.5
done

# Verify session ID from bridge
BRIDGE_SESSION=$(curl -s http://localhost:8081/status 2>/dev/null | jq -r '.sessionId' 2>/dev/null)
if [ "$BRIDGE_SESSION" == "$SESSION_ID" ]; then
    echo "[Startup] ✓ Session ID verified: $BRIDGE_SESSION"
else
    echo "[Startup] ✗ WARNING: Session mismatch (expected $SESSION_ID, got $BRIDGE_SESSION)"
fi

# Start input daemon (handles all button presses)
echo "[Startup] Starting input daemon..."
~/.claude/tkeyboard-input-daemon.sh "$SESSION_ID" "$CLAUDE_PID" "$TTY_PATH" > /tmp/tkeyboard-daemon-$SESSION_ID.log 2>&1 &
DAEMON_PID=$!

echo "[Startup] Input daemon started (PID=$DAEMON_PID)"

# Save session info
cat > ~/.claude/active-session.json <<EOF
{
  "sessionId": "$SESSION_ID",
  "claudePid": $CLAUDE_PID,
  "tty": "$TTY_PATH",
  "bridgePid": $BRIDGE_PID,
  "daemonPid": $DAEMON_PID,
  "startTime": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF

echo "[Startup] Session info saved to ~/.claude/active-session.json"
echo ""

# Register cleanup trap
cleanup() {
    echo ""
    echo "[Cleanup] Shutting down session $SESSION_ID..."

    # Kill daemon first
    if kill -0 "$DAEMON_PID" 2>/dev/null; then
        echo "[Cleanup] Stopping daemon (PID=$DAEMON_PID)"
        kill "$DAEMON_PID" 2>/dev/null
    fi

    # Kill bridge server
    if kill -0 "$BRIDGE_PID" 2>/dev/null; then
        echo "[Cleanup] Stopping bridge server (PID=$BRIDGE_PID)"
        kill "$BRIDGE_PID" 2>/dev/null
    fi

    # Remove session file
    rm -f ~/.claude/active-session.json

    echo "[Cleanup] Session $SESSION_ID terminated"
}

trap cleanup EXIT INT TERM

echo "╔════════════════════════════════════════╗"
echo "║   T-Keyboard Ready!                    ║"
echo "╠════════════════════════════════════════╣"
echo "║   Press any button - text auto-injects ║"
echo "║   Press STOP to interrupt              ║"
echo "║   Press Ctrl+C to exit                 ║"
echo "╚════════════════════════════════════════╝"
echo ""
echo "Monitoring keyboard inputs..."
echo ""

# Monitor loop
while true; do
    # Check if processes are still alive
    if ! kill -0 "$BRIDGE_PID" 2>/dev/null; then
        echo "[Monitor] ERROR: Bridge server died!"
        exit 1
    fi

    if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
        echo "[Monitor] ERROR: Input daemon died!"
        exit 1
    fi

    # Poll for inputs
    RESPONSE=$(curl -s http://localhost:8081/hook/get-inputs 2>/dev/null)

    if [ $? -eq 0 ] && [ ! -z "$RESPONSE" ]; then
        INPUTS=$(echo "$RESPONSE" | jq -r '.inputs' 2>/dev/null)

        if [ "$INPUTS" != "[]" ] && [ "$INPUTS" != "null" ]; then
            echo "$RESPONSE" | jq -c '.inputs[]' 2>/dev/null | while read -r input; do
                BUTTON_TEXT=$(echo "$input" | jq -r '.text')
                TIMESTAMP=$(echo "$input" | jq -r '.timestamp')
                echo "[$(date -r $((TIMESTAMP / 1000)) '+%H:%M:%S')] Button pressed: $BUTTON_TEXT"
            done
        fi
    fi

    sleep 2
done
