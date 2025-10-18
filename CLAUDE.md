# T-Keyboard-S3 Claude Code Controller - Technical Context

## Architecture: Agent-Based

This project uses a **Claude agent** that monitors for keyboard inputs and processes them immediately. No hooks or terminal automation needed.

### The Flow
```
T-Keyboard Press → Bridge Server Queue → Claude Agent (polls) → Immediate Response
```

## Key Components

### 1. ESP32 Firmware (`arduino/TKeyboardClaude/`)
- WebSocket client connects to bridge server
- WiFi configuration via AP mode (192.168.4.1)
- 4x 128x128 displays with individual CS pins
- RGB LEDs for status (GPIO 11)
- SPIFFS for image caching (~13MB partition)

### 2. Bridge Server (`bridge-server/agent-bridge.js`)
- Simple WebSocket server on port 8080
- HTTP API on port 8081 for agent
- Queues keyboard inputs
- No complex state management

### 3. Claude Agent (runs in Claude Code)
- Polls `/hook/get-inputs` every 2 seconds
- Processes keyboard inputs immediately
- Updates display based on context
- Can start bridge server itself

## Hardware Details

**T-Keyboard-S3 V1.0** (no "Pro" version exists):
- ESP32-S3-R8: 16MB Flash, 8MB PSRAM (OPI mode)
- Keys: GPIO 10, 9, 46, 3 (KEY4 = BOOT)
- Display CS: GPIO 12, 13, 14, 21
- Shared SPI: DC=45, CLK=47, MOSI=48, RST=38, BL=39

## Critical Settings

**Arduino IDE:**
- Board: ESP32S3 Dev Module
- PSRAM: **OPI PSRAM** (not QIO!)
- Partition: Custom (3MB app, 13MB SPIFFS)

**Image Format:**
- 128x128 pixels
- RGB565 (2 bytes per pixel)
- Big-endian byte order

**Display Guidelines:**
- Minimum text size: 2 (setTextSize(2))
- Size 1 text is too small and nearly unreadable
- Use size 2 for labels, size 3-4 for primary content

## Agent Capabilities

The Claude agent can:
- Start the bridge server via Bash
- Monitor for inputs without blocking
- Generate custom images dynamically
- Detect conversation context
- Update button options in real-time

## Serial Configuration

Configure WiFi and bridge settings via serial (115200 baud):

```
WIFI:MySSID:MyPassword         - Set WiFi credentials
HOST:192.168.1.100             - Set bridge server host
PORT:8080                      - Set bridge server port
CONFIG                         - Enter AP config mode
STATUS                         - Show current settings
RESTART                        - Restart device
```

## Quick Test

```bash
# Test bridge server
curl http://localhost:8081/status

# Simulate button press
curl -X POST http://localhost:8081/update \
  -H "Content-Type: application/json" \
  -d '{"type":"key_press","key":1,"text":"Yes"}'

# Check queue
curl http://localhost:8081/hook/get-inputs
```

## Why This Approach?

1. **Simple**: One agent prompt starts everything
2. **Non-blocking**: Doesn't interfere with typing
3. **Immediate**: 2-second max response time
4. **Smart**: Full context awareness
5. **Clean**: No hooks, no terminal hacks

## Common Issues

**PSRAM Not Found**: Must select OPI PSRAM in Arduino IDE
**Images Missing**: Run `npm run generate-images`
**Can't Connect**: Check computer IP in keyboard config

## File Structure
```
tkeyboard-claude/
├── arduino/TKeyboardClaude/  # ESP32 firmware
├── bridge-server/             # Node.js bridge
│   ├── agent-bridge.js        # Main server
│   └── generate-images.js     # Icon generator
├── images/                    # Generated icons
├── AGENT_PROMPT.md           # Ready-to-use agent
└── README.md                 # User documentation
```

## The Agent Prompt

Just paste this to start everything:

```
I need you to set up and monitor my T-Keyboard device. Please:
1. Start the bridge server in tkeyboard-claude/bridge-server
2. Monitor http://localhost:8081/hook/get-inputs every 2 seconds
3. Process keyboard inputs immediately
4. Update display based on context
Start now.
```