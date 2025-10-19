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

3. Maintain context-aware button options with images by preference:
   - Error context → 🐛 Debug, 🔄 Retry, ⏭️ Skip, ❓ Help
   - Code review → ▶️ Run, 🧪 Test, ♻️ Refactor, 📖 Explain
   - Questions → ✅ Yes, ❌ No, ℹ️ More Info, 💡 Example
   - Git operations → 💾 Commit, 🔀 Merge, 📤 Push, ↩️ Revert
   - Default → ✅ Yes, ❌ No, ▶️ Continue, ❓ Help

   IMPORTANT - Display vs Action Text:
   - Use images/emojis for display (visual recognition)
   - Provide detailed action text that gets sent when pressed
   - Example: Display "🛑" but send "Please stop what you're doing and wait"

4. Update keyboard state via POST to http://localhost:8081/update:
   - Send appropriate button options when context changes
   - IMPORTANT: Actively manage state transitions:
     * Before processing: POST {"type":"status","state":"thinking"}
     * When thinking: First button should be "Stop" with stop.rgb image
     * After completing: POST {"type":"status","state":"idle"}
     * On API error: POST {"type":"status","state":"error","message":"error text"}
     * On rate limit: Try to POST {"type":"status","state":"limit","countdown":<retry_after>}
       (Extract retry_after from error response if available, otherwise bridge auto-detects)

   - Button update format (use images by preference):
     ```json
     {
       "buttons": ["STOP", "Yes", "No", "Info"],
       "actions": [
         "Please stop what you're doing and wait for further instructions",
         "Yes, I agree to proceed with this operation",
         "No, cancel this operation",
         "Give me more information about what will happen"
       ],
       "images": ["stop.rgb", "yes.rgb", "no.rgb", "info.rgb"]
     }
     ```

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
     "buttons": ["🛑", "✅", "❌", "ℹ️"],
     "actions": [
       "Stop and wait for further instructions",
       "Yes, proceed with the operation",
       "No, cancel the operation",
       "Tell me more about what will happen"
     ],
     "images": ["stop.rgb", "yes.rgb", "no.rgb", "info.rgb"]
   }
   ```
   - Display shows emoji/image (compact, visual)
   - Action contains full text sent when pressed (detailed, contextual)
   - If actions omitted, buttons text is used as action

7. Claude TUI prompt detection:
   - Watch for "[Yes/Always/No]" patterns in output
   - Auto-update keyboard to match expected inputs
   - Saves user from typing common responses
   - Example: File overwrite → ["💾 Overwrite", "❌ Cancel", "", ""]

Start the bridge server and begin monitoring with image generation.
```

See `DYNAMIC_IMAGES.md` for implementation details.