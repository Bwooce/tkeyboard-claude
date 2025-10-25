#!/bin/bash
# T-Keyboard Monitor Script
# Auto-generated - manages bridge server and monitors session health

cd /Users/bruce/Documents/Arduino/tkeyboard-claude

# Detect the actual Claude Code session PID
CLAUDE_PID=$(ps aux | grep -E "^\s*bruce\s+[0-9]+.*\s+claude\s*$" | grep -v grep | awk '{print $2}' | head -1)

if [ -z "$CLAUDE_PID" ]; then
    echo "⚠️ Could not detect Claude PID - exiting"
    exit 1
fi

echo "Detected Claude PID: $CLAUDE_PID"

# Check if bridge server is running, start if needed
if ! curl -s http://localhost:8081/status > /dev/null 2>&1; then
    echo "Starting bridge server with CLAUDE_PID=$CLAUDE_PID"
    cd bridge-server
    CLAUDE_PID=$CLAUDE_PID CLAUDE_SESSION_ID="tk-$(date +%s)-$CLAUDE_PID" node agent-bridge.js > /tmp/bridge-server.log 2>&1 &
    sleep 2
    cd ..
fi

# Store session info
INITIAL_SESSION_ID=$(curl -s http://localhost:8081/status 2>/dev/null | jq -r '.sessionId')
INITIAL_CLAUDE_PID=$(curl -s http://localhost:8081/status 2>/dev/null | jq -r '.claudePid')

echo "Initial Session ID: $INITIAL_SESSION_ID"
echo "Bridge tracking PID: $INITIAL_CLAUDE_PID"

# Verify we got the right PID
if [ "$INITIAL_CLAUDE_PID" != "$CLAUDE_PID" ]; then
    echo "⚠️ Warning: Bridge tracking PID $INITIAL_CLAUDE_PID but actual Claude is PID $CLAUDE_PID"
fi

# Set default buttons on keyboard
curl -s -X POST http://localhost:8081/update \
  -H 'Content-Type: application/json' \
  -d '{"buttons":["Yes","No","Proceed","Help"],"actions":["Yes","No","Proceed","Help"],"images":["yes.rgb","no.rgb","proceed.rgb","help.rgb"]}' \
  > /dev/null 2>&1

echo "Default buttons set"

# Start input daemon for button press handling
bash installation/tkeyboard-input-daemon.sh "$INITIAL_SESSION_ID" "$INITIAL_CLAUDE_PID" > /tmp/tkeyboard-input-daemon.log 2>&1 &
INPUT_DAEMON_PID=$!
echo "Input daemon started (PID $INPUT_DAEMON_PID)"

echo "Monitoring session $INITIAL_SESSION_ID (PID $INITIAL_CLAUDE_PID) - Default buttons set, input daemon running"

# Health Monitoring Loop (Every 2 Seconds)
while true; do
    # Get status (silent)
    STATUS=$(curl -s http://localhost:8081/status 2>/dev/null)

    # Check bridge responding
    if [ -z "$STATUS" ]; then
        echo "⚠️ Bridge died - restarting"
        cd bridge-server
        CLAUDE_PID=$INITIAL_CLAUDE_PID CLAUDE_SESSION_ID="$INITIAL_SESSION_ID" node agent-bridge.js > /tmp/bridge-server.log 2>&1 &
        sleep 5
        cd ..
        continue
    fi

    # Validate session
    CURRENT_SESSION=$(echo "$STATUS" | jq -r '.sessionId' 2>/dev/null)
    CURRENT_PID=$(echo "$STATUS" | jq -r '.claudePid' 2>/dev/null)

    if [ "$CURRENT_SESSION" != "$INITIAL_SESSION_ID" ]; then
        echo "⚠️ Session changed - terminating"
        kill $INPUT_DAEMON_PID 2>/dev/null
        exit 0
    fi

    # Check main session alive
    if ! kill -0 "$CURRENT_PID" 2>/dev/null; then
        echo "⚠️ Main session died (PID $CURRENT_PID) - terminating"
        kill $INPUT_DAEMON_PID 2>/dev/null
        exit 0
    fi

    # All updates silent
    sleep 2
done
