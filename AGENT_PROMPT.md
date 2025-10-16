# T-Keyboard Agent - Self-Contained

Copy and paste this entire prompt into Claude Code to start everything:

```
I need you to set up and monitor my T-Keyboard device. Please:

1. First, start the bridge server:
   - Navigate to tkeyboard-claude/bridge-server
   - Run `npm install` if needed, then `npm start` in the background
   - Confirm it's running on port 8081

2. Then begin monitoring:
   - Every 2 seconds, check http://localhost:8081/hook/get-inputs
   - Process any keyboard inputs immediately as my responses
   - Update keyboard display based on our conversation context

3. Maintain context-aware button options:
   - Error context → Debug, Retry, Skip, Help
   - Code review → Run, Test, Refactor, Explain
   - Questions → Yes, No, More Info, Example
   - Default → Yes, No, Continue, Help

4. Update keyboard state via POST to http://localhost:8081/update:
   - Send appropriate button options when context changes
   - IMPORTANT: Actively manage state transitions:
     * Before processing: POST {"type":"status","state":"thinking"}
     * When thinking: First button should be "Stop" with stop.rgb image
     * After completing: POST {"type":"status","state":"idle"}
     * On API error: POST {"type":"status","state":"error","message":"error text"}
     * Note: Rate limits are auto-detected by bridge server (no action needed)

5. Generate custom icons for new contexts:
   - Check if icon exists in bridge-server/images/cache/
   - If not, generate using: node bridge-server/generate-images.js --emoji "🐛" debug
   - Or for text icons: node bridge-server/generate-images.js --text "RUN" "#00FF00" run
   - Include generated images in update messages to keyboard
   - Keyboard will cache automatically in SPIFFS

Start the bridge server now and begin monitoring.
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
Monitor my T-Keyboard at http://localhost:8081/hook/get-inputs
Process any inputs immediately and keep the display updated with context-appropriate options.
Start monitoring now.
```

## Advanced: With Dynamic Image Generation

For custom icons generated on-the-fly:

```
I need you to set up my T-Keyboard with dynamic image generation:

1. Start the bridge server in tkeyboard-claude/bridge-server

2. Monitor http://localhost:8081/hook/get-inputs every 2 seconds
   - Process inputs immediately as my responses

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

Start the bridge server and begin monitoring with image generation.
```

See `DYNAMIC_IMAGES.md` for implementation details.