# T-Keyboard Agent - TTY Injection

Copy and paste this entire prompt into Claude Code to start everything:

```
I need you to set up my T-Keyboard with TTY injection. Please:

1. Check if bridge server is running:
   - Check http://localhost:8081/status
   - If not running, start it: cd tkeyboard-claude/bridge-server && node agent-bridge.js (in background)
   - Confirm keyboard is connected

2. Start the TTY injection daemon:
   - Run: ~/.claude/tkeyboard-agent-start.sh
   - This auto-detects Claude PID and TTY
   - Starts input daemon that injects button presses directly to terminal
   - Button presses will appear as if I typed them

3. Monitor context and update button display:
   - Poll http://localhost:8081/inputs every 2 seconds to keep bridge healthy
   - Update keyboard buttons based on conversation context
   - Send state updates when thinking/idle

   ⚠️ CRITICAL - Agent Health Monitoring:
   You MUST check on every polling cycle (every 2 seconds):

   ```bash
   # Get session info from bridge
   RESPONSE=$(curl -s http://localhost:8081/status)

   # Extract main session PID
   CLAUDE_PID=$(echo "$RESPONSE" | jq -r '.claudePid')

   # Check if main Claude session is still alive
   if ! kill -0 "$CLAUDE_PID" 2>/dev/null; then
       echo "Main Claude session (PID $CLAUDE_PID) died. Agent terminating."
       exit 0
   fi
   ```

   **Additional health checks:**
   - Store the initial session ID when you start
   - If session ID changes between polls → New session started, terminate agent
   - If bridge returns error/timeout → Attempt to restart bridge server
   - If TTY path doesn't exist → Main session probably died, terminate

   **Agent behavior:**
   - If main session dies → Agent terminates immediately
   - If bridge dies → Agent restarts bridge (don't terminate)
   - If session ID changes → Agent terminates (new session took over)
   - You are the supervisor - keep the system running even if bridge crashes

3. Maintain context-aware button options with images by preference:
   - Error context → 🐛 Debug, 🔄 Retry, ⏭️ Skip, ❓ Help
   - Code review → ▶️ Run, 🧪 Test, ♻️ Refactor, 📖 Explain
   - Questions → ✅ Yes, ❌ No, ℹ️ More Info, 💡 Example
   - Git operations → 💾 Commit, 🔀 Merge, 📤 Push, ↩️ Revert
   - Default → ✅ Yes, ❌ No, ▶️ Continue, ❓ Help

   IMPORTANT - Display vs Action Text:
   - Use images for display (visual recognition)
   - Action text is what gets LITERALLY INJECTED to terminal
   - For Claude prompts: Match expected input (e.g., "Yes" not "Yes, I agree")
   - For custom questions: Can use detailed text (e.g., "Please explain this in detail")

4. Update keyboard state via POST to http://localhost:8081/update:
   - Send appropriate button options when context changes
   - IMPORTANT: Actively manage state transitions:

     **BEFORE starting any task execution:**
     ```bash
     curl -s -X POST http://localhost:8081/update \
       -H 'Content-Type: application/json' \
       -d '{"buttons":["STOP","","",""],"actions":["STOP","","",""],"images":["stop.rgb","","",""]}'
     ```

     **AFTER completing task execution:**
     ```bash
     curl -s -X POST http://localhost:8081/update \
       -H 'Content-Type: application/json' \
       -d '{"buttons":["Yes","No","Proceed","Help"],"actions":["Yes","No","Proceed","Help"],"images":["yes.rgb","no.rgb","proceed.rgb","help.rgb"]}'
     ```

     * On API error: POST {"type":"status","state":"error","message":"error text"}
     * On rate limit: Try to POST {"type":"status","state":"limit","countdown":<retry_after>}
       (Extract retry_after from error response if available, otherwise bridge auto-detects)

   - Button update format (use images by preference):
     ```json
     {
       "buttons": ["STOP", "Yes", "No", "Info"],
       "actions": ["STOP", "Yes", "No", "Info"],
       "images": ["stop.rgb", "yes.rgb", "no.rgb", "info.rgb"]
     }
     ```

   - ⚠️ CRITICAL - Action Text Rules:
     * Action text is LITERALLY INJECTED to terminal as if user typed it
     * For Claude Code prompts ([Yes/Always/No]): Use SHORT exact answers: ["Yes", "Always", "No"]
     * For conversational input: Use verbose text: ["Please explain this in detail"]
     * DON'T use verbose actions for simple prompts - it creates duplicate messages!

   - IMPORTANT: buttons array should contain ASCII text only (no emojis)
   - Emojis should be sent as .rgb image files in the images array
   - UTF-8 emojis in the text field will display as garbage characters
   - If actions array is omitted, buttons text is used as action
   - If images array is omitted or empty, text is displayed instead
   - Action text can be detailed (up to ~200 chars), display should be brief

5. Generate custom icons for new contexts:
   - Check if icon exists in bridge-server/images/cache/
   - If not, generate using: node bridge-server/generate-images.js --emoji "🐛" debug
   - Or for text icons: node bridge-server/generate-images.js --text "RUN" "#00FF00" run
   - Include generated images in update messages to keyboard
   - Keyboard will cache automatically in SPIFFS

Start the bridge server now and begin monitoring.

SPECIAL HANDLING - Claude TUI Prompts:
When you detect Claude's standard prompts (e.g., "[Yes/Always/No]"), automatically update keyboard:
- Yes/Always/No → buttons: ["✅ Yes", "⚠️ Always", "❌ No", "❓ Help"]
- Overwrite/Cancel → buttons: ["💾 Overwrite", "❌ Cancel", "", ""]
- Continue/Stop → buttons: ["▶️ Continue", "🛑 Stop", "", ""]
This provides immediate button access to common prompts without typing.
```

## That's It!

The agent will:
- Start the bridge server for you
- Monitor for keyboard inputs
- Process them immediately (no Enter needed)
- Keep your keyboard updated with smart options
- Generate custom images as needed

## If Bridge Server Is Already Running

Use this simpler prompt:

```
Set up my T-Keyboard with TTY injection:

1. Start the TTY injection daemon: ~/.claude/tkeyboard-agent-start.sh
   - This will detect my Claude session and TTY
   - Starts background daemon that injects button presses to terminal

2. Monitor and update keyboard display:
   - Poll http://localhost:8081/inputs every 2 seconds (keeps bridge healthy)
   - Update button options based on conversation context
   - When I'm thinking, update to show STOP button

Note: Button presses will appear in terminal automatically via TTY injection.
You don't need to watch for them - they'll appear as if I typed them.

Start the daemon now.
```

## Advanced: With Dynamic Image Generation

For custom icons generated on-the-fly:

```
I need you to set up my T-Keyboard with TTY injection and dynamic images:

1. Start the TTY injection daemon: ~/.claude/tkeyboard-agent-start.sh

2. Monitor http://localhost:8081/inputs every 2 seconds
   - Keep bridge server healthy
   - Update button display based on context

3. Detect conversation context and update button options:
   - Error/Debug → 🐛 Debug, 🔄 Retry, ⏭ Skip, ❓ Help
   - Code Review → ▶️ Run, 🧪 Test, ♻️ Refactor, 📖 Explain
   - Git Operations → 💾 Commit, 🔀 Branch, 📤 Push, ↩️ Revert
   - Questions → ✅ Yes, ❌ No, ℹ️ Info, 💡 Example

4. Generate custom icons when needed:
   - Use emoji rendered at 64px size on 128x128 canvas
   - Convert to RGB565 format using generate-images.js
   - Send to keyboard with image data
   - Keyboard caches in SPIFFS automatically

5. Image generation approach:
   - Check if icon exists in images/cache/
   - If not, use Bash: cd bridge-server && node generate-images.js --emoji "🐛"
   - Or generate inline with simple shapes/text

6. Button format with display vs action separation:
   ```json
   POST http://localhost:8081/update
   {
     "buttons": ["STOP", "Yes", "No", "Info"],
     "actions": ["STOP", "Yes", "No", "Info"],
     "images": ["stop.rgb", "yes.rgb", "no.rgb", "info.rgb"]
   }
   ```
   - Display shows image from images array (compact, visual)
   - Action text is LITERALLY injected to terminal as if user typed it
   - For Claude prompts: Use SHORT exact answers matching expected input
   - For custom questions: Can use verbose text for conversational responses
   - If actions omitted, buttons text is used as action

7. Claude TUI prompt detection:
   - Watch for "[Yes/Always/No]" patterns in output
   - Auto-update keyboard to match expected inputs
   - Saves user from typing common responses
   - Example: File overwrite → ["💾 Overwrite", "❌ Cancel", "", ""]

Start the bridge server and begin monitoring with image generation.
```

## Agent Self-Termination

**CRITICAL:** To prevent orphaned agents, implement health monitoring in your polling loop:

```bash
# Store initial session info
INITIAL_SESSION_ID=$(curl -s http://localhost:8081/status | jq -r '.sessionId')
INITIAL_CLAUDE_PID=$(curl -s http://localhost:8081/status | jq -r '.claudePid')

echo "Monitoring session: $INITIAL_SESSION_ID (PID $INITIAL_CLAUDE_PID)"

# In your polling loop (every 2 seconds):
while true; do
    # Get current status
    STATUS=$(curl -s http://localhost:8081/status)

    if [ $? -ne 0 ] || [ -z "$STATUS" ]; then
        echo "Bridge server not responding - attempting restart..."
        cd tkeyboard-claude/bridge-server && node agent-bridge.js &
        sleep 5
        continue
    fi

    # Check session consistency
    CURRENT_SESSION=$(echo "$STATUS" | jq -r '.sessionId')
    CURRENT_PID=$(echo "$STATUS" | jq -r '.claudePid')

    if [ "$CURRENT_SESSION" != "$INITIAL_SESSION_ID" ]; then
        echo "Session changed from $INITIAL_SESSION_ID to $CURRENT_SESSION - terminating agent"
        exit 0
    fi

    # Check if main Claude session still alive
    if ! kill -0 "$CURRENT_PID" 2>/dev/null; then
        echo "Main Claude session (PID $CURRENT_PID) died - terminating agent"
        exit 0
    fi

    # Process inputs and update keyboard...

    sleep 2
done
```

This ensures the agent dies when the main session is gone, preventing runaway processes.

See `DYNAMIC_IMAGES.md` for implementation details.