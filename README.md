# T-Keyboard-S3 Claude Code Controller

> **⚠️ UNTESTED**: This project is fresh from development and has not been tested on actual hardware yet. Consider this an alpha release. Contributions and testing feedback welcome!

Interactive hardware controller for Claude Code using the LILYGO T-Keyboard-S3 (4-key version).

## What It Does

- **4 Context-Sensitive Buttons**: Each key shows dynamic options on its 128x128 display
- **Immediate Response**: Press a button, Claude responds within 2 seconds
- **Visual Feedback**: RGB LEDs show status (thinking, ready, error)
- **Smart Adaptation**: Buttons change based on conversation context
- **Non-Blocking**: Works alongside normal typing in Claude Code

## Quick Start (5 Minutes)

### 1. Upload Firmware

Open Arduino IDE with these settings:
- Board: **ESP32S3 Dev Module**
- PSRAM: **OPI PSRAM** (critical!)
- Partition: Use included `partitions.csv`
- Flash Size: **16MB**

Upload: `arduino/TKeyboardClaude/TKeyboardClaude.ino`

### 2. Configure WiFi

1. Device creates AP: **TKeyboard-Setup**
2. Connect with password: **12345678**
3. Browse to: **192.168.4.1**
4. Enter:
   - Your WiFi credentials
   - Your computer's IP address
   - Port: 8081

### 3. Start with Claude Agent

Just paste this into your Claude Code session:

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

Start the bridge server now and begin monitoring.
```

That's it! Press buttons on your T-Keyboard and Claude responds immediately.

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      T-Keyboard-S3                          │
│  ┌───────┐  ┌───────┐  ┌───────┐  ┌───────┐               │
│  │128x128│  │128x128│  │128x128│  │128x128│  Displays     │
│  │  Key1 │  │  Key2 │  │  Key3 │  │  Key4 │               │
│  └───┬───┘  └───┬───┘  └───┬───┘  └───┬───┘               │
│      │          │          │          │                     │
│  ┌───┴──────────┴──────────┴──────────┴───┐               │
│  │     ESP32-S3 (16MB Flash, 8MB PSRAM)   │               │
│  │     WiFi • WebSocket • SPIFFS Cache     │               │
│  └─────────────────┬────────────────────────┘              │
└────────────────────┼───────────────────────────────────────┘
                     │ WiFi
                     ↓
         ┌───────────────────────┐
         │   Bridge Server       │
         │   (Node.js)           │
         │                       │
         │  • WebSocket (8080)   │  ← T-Keyboard connects
         │  • HTTP API  (8081)   │  ← Agent polls
         │  • Input Queue        │
         └───────────┬───────────┘
                     │ HTTP Poll (every 2s)
                     ↓
         ┌───────────────────────┐
         │   Claude Agent        │
         │                       │
         │  1. Polls for inputs  │
         │  2. Processes input   │
         │  3. Detects context   │
         │  4. Updates display   │
         │  5. Generates images  │
         └───────────────────────┘
```

**Flow:**
1. User presses button → Queued in bridge server
2. Agent polls → Finds input → Processes immediately
3. Agent analyzes context → Updates button options
4. T-Keyboard receives new display config via WebSocket

## Context Examples

**During Rate Limit:**
- Display 1: Shows countdown or elapsed time
- Display 2: **Continue** (green) - Try to resume when ready
- Display 3: Blank
- Display 4: Blank

**During Errors:**
- Display 1: Pulsing "!" with "Error" and "Check console"
- Display 2: **Continue** (green) - Dismiss error and resume
- Display 3: **Retry** (orange) - Retry the last operation
- Display 4: Blank

**Code Review:**
- Run | Test | Refactor | Explain

**Questions:**
- Yes | No | More Info | Example

## Project Structure

```
tkeyboard-claude/
├── arduino/
│   └── TKeyboardClaude/     # ESP32 firmware
├── bridge-server/           # Simple WebSocket bridge
│   ├── agent-bridge.js      # Main server
│   └── generate-images.js   # Image generator
└── images/                  # Generated icons
```

## Hardware Specs

- **Board**: LILYGO T-Keyboard-S3 V1.0
- **MCU**: ESP32-S3-R8 (16MB Flash, 8MB PSRAM)
- **Displays**: 4x 128x128 TFT (GC9107)
- **Keys**: 4x hot-swappable switches (Kailh)
- **LEDs**: 4x WS2812C RGB

### Hardware Reference
- [Official Repository](https://github.com/Xinyuan-LilyGO/T-Keyboard-S3) - Schematics, examples, documentation
- [Pinout & Specifications](https://github.com/Xinyuan-LilyGO/T-Keyboard-S3#pinout) - Detailed pin mapping
- [Sample Code](https://github.com/Xinyuan-LilyGO/T-Keyboard-S3/tree/main/examples) - Arduino examples
- [Product Page](https://www.lilygo.cc/products/t-keyboard-s3) - Purchase and specs

## Advanced Features

### Generate Custom Images

The system includes basic icons (yes, no, stop, continue, etc.):

```bash
cd bridge-server
npm run generate-images
```

### Dynamic Image Generation

Since we already have context-aware button text, adding dynamic image generation requires minimal work:

**What's Already Done:**
- ✅ Agent detects conversation context
- ✅ Agent updates button options dynamically
- ✅ Image conversion to RGB565 format
- ✅ WebSocket image transfer protocol
- ✅ SPIFFS caching system

**What's Needed (< 1 hour):**

1. **Add image generation to agent** - The agent already analyzes context, just needs to generate images:
   ```javascript
   // In agent, when context changes:
   if (!imageExists('debug.rgb')) {
       generateDebugIcon();  // Use Canvas/PIL to draw
       convertToRGB565();     // Already implemented
       sendToKeyboard();      // Already implemented
   }
   ```

2. **Image generation methods** - Add simple drawing functions:
   - Text-based icons (render text to image)
   - Shape-based icons (draw circles, arrows, symbols)
   - Icon font rendering (use emoji or icon fonts)

3. **Caching strategy** - Already implemented in ESP32:
   - Check if image exists in SPIFFS
   - Download and cache if new
   - Reuse cached images

**Example Implementation:**

Add to the agent prompt:
```
When updating button options:
1. Detect context (already doing this)
2. Choose appropriate icons
3. If custom icon needed, generate 128x128 image
4. Convert to RGB565 format
5. Send via WebSocket to keyboard
6. Keyboard caches in SPIFFS
```

**Effort Estimate:** ~30-60 minutes since:
- Image conversion already works (`generate-images.js`)
- WebSocket transfer already implemented
- ESP32 caching already functional
- Just need agent to call image generation when detecting new contexts

### Manual Bridge Start

If you prefer to start the bridge separately:

```bash
cd bridge-server
npm install
npm start
```

Then use this shorter agent prompt:
```
Monitor my T-Keyboard at http://localhost:8081/hook/get-inputs
Process inputs immediately and update display with context options.
```

### LED Status Indicators

The 4 RGB LEDs show system status:

- 🟢 **Green (solid)**: Connected and ready
- 🟡 **Yellow (pulsing)**: Claude is thinking/processing
- 🔴 **Red (solid)**: Error state or config mode
- 🔴 **Red (slow pulse)**: Rate limited - waiting to retry
- 🔴 **Red (fast blink)**: WebSocket not connected - check bridge server
- 🔵 **Blue (breathing)**: WiFi config mode active or waiting for input
- 🔵 **Blue (pulsing)**: Connecting to WiFi

### Configuration

- **Backlight**: Adjustable via web config (default 50%)
- **WiFi Reset**: Press KEY1+KEY4 simultaneously
- **Bridge Port**: Default 8081 (configurable)

## Troubleshooting

**"PSRAM not found"**
- Must select "OPI PSRAM" not "QIO PSRAM"

**"T-Keyboard not connecting"**
- Check bridge server is running
- Verify computer's IP in keyboard config
- Test: `curl http://localhost:8081/status`

**"Images not showing"**
- SPIFFS auto-formats on first boot
- Run image generator: `npm run generate-images`

## Why This Design?

- **Agent-Based**: Simple polling approach with automatic failover
- **No Blocking**: Doesn't interfere with typing
- **Immediate Response**: 2-second max latency
- **Smart Context**: Adapts to conversation
- **Rate Limit Detection**: Bridge server detects when agent stops (likely rate limited) and automatically updates keyboard
- **Future-Proof**: Easy to extend

### Rate Limit Handling

**Dual-Mode Detection:**

1. **Agent Reports (Best)**: When the agent catches a rate limit error, it extracts the `retry_after` value and sends it before stopping
   - Shows accurate countdown timer
   - User knows exactly when to retry

2. **Bridge Detects (Fallback)**: If agent doesn't report (or can't), bridge server monitors polling
   - If no poll for 10 seconds → Assumes rate limit
   - Shows elapsed time with animated "Waiting..." (no fake countdown)
   - Automatically detects when agent resumes

**Why Both?**
- Agent **tries** to report retry_after from error response (if it can)
- But rate limits might stop agent before it reports
- Bridge server catches this case with monitoring
- Result: Always get feedback, countdown when available

**Note:** The Claude API error response includes retry-after duration in seconds. The agent attempts to extract and report this before fully stopping.

## License

MIT