#!/bin/bash
# T-Keyboard Input Daemon
# Monitors for all button presses and injects text/commands to specific Claude session
# - STOP button: Sends Esc (stops generation)
# - Other buttons: Injects button text + Enter (autonomous input)

if [ $# -ne 2 ]; then
    echo "Usage: $0 SESSION_ID CLAUDE_PID"
    exit 1
fi

SESSION_ID="$1"
CLAUDE_PID="$2"

echo "[Input Daemon] Starting for session $SESSION_ID (PID=$CLAUDE_PID)"

# Validate Claude process exists
if ! kill -0 "$CLAUDE_PID" 2>/dev/null; then
    echo "[Input Daemon] ERROR: Claude process $CLAUDE_PID not found"
    exit 1
fi

# Poll bridge server for STOP button presses
while true; do
    # Check if Claude process is still alive
    if ! kill -0 "$CLAUDE_PID" 2>/dev/null; then
        echo "[Stop Daemon] Claude process $CLAUDE_PID died, exiting"
        exit 0
    fi

    # Poll bridge server (100ms interval)
    RESPONSE=$(curl -s http://localhost:8081/inputs 2>/dev/null)

    if [ $? -eq 0 ] && [ ! -z "$RESPONSE" ]; then
        # Extract session ID from response
        RESPONSE_SESSION=$(echo "$RESPONSE" | jq -r '.sessionId' 2>/dev/null)

        # Validate session ID matches
        if [ "$RESPONSE_SESSION" != "$SESSION_ID" ]; then
            echo "[Stop Daemon] WARNING: Session mismatch (expected $SESSION_ID, got $RESPONSE_SESSION)"
            sleep 0.1
            continue
        fi

        # Check for inputs
        INPUTS=$(echo "$RESPONSE" | jq -r '.inputs' 2>/dev/null)

        if [ "$INPUTS" != "[]" ] && [ "$INPUTS" != "null" ]; then
            # Process each input
            echo "$RESPONSE" | jq -c '.inputs[]' 2>/dev/null | while read -r input; do
                BUTTON_TEXT=$(echo "$input" | jq -r '.text')
                BUTTON_KEY=$(echo "$input" | jq -r '.key')

                if [ ! -z "$BUTTON_TEXT" ]; then
                    if [ "$BUTTON_TEXT" == "STOP" ]; then
                        # STOP button - send Esc (key code 53) WITHOUT Enter
                        echo "[Input Daemon] STOP button pressed! Sending Esc"
                        osascript -e 'tell application "System Events" to key code 53' > /dev/null 2>&1

                        if [ $? -eq 0 ]; then
                            echo "[Input Daemon] Esc sent successfully"
                        else
                            echo "[Input Daemon] ERROR: Failed to send Esc"
                        fi

                        sleep 1  # Cooldown after interrupt
                    else
                        # Normal button - inject text + Enter using AppleScript
                        echo "[Input Daemon] Button $BUTTON_KEY pressed: '$BUTTON_TEXT' - Injecting via AppleScript"

                        # Use AppleScript to simulate keypresses (requires Accessibility permissions)
                        osascript -e "tell application \"System Events\" to keystroke \"$BUTTON_TEXT\"" > /dev/null 2>&1
                        osascript -e 'tell application "System Events" to key code 36' > /dev/null 2>&1

                        if [ $? -eq 0 ]; then
                            echo "[Input Daemon] Text injected successfully: '$BUTTON_TEXT'"
                        else
                            echo "[Input Daemon] ERROR: Failed to inject text via AppleScript"
                        fi

                        sleep 0.2  # Small cooldown between button presses
                    fi
                fi
            done
        fi
    fi

    # Sleep 100ms between polls
    sleep 0.1
done
