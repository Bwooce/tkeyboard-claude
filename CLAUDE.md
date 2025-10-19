# T-Keyboard-S3 Claude Code Controller - Technical Context

## Architecture: Agent + Hooks

This project uses a **Claude agent** for monitoring inputs and **hooks** for visual state management.

### The Flow
```
Button Press:  T-Keyboard → Bridge → Agent polls → Immediate Response
Thinking State: PreToolUse hook → STOP button → PostToolUse hook → Default buttons
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

### 3. Claude Agent (runs in Claude Code via Task tool)
- Polls `/inputs` every 2 seconds
- Monitors main session health (auto-terminates if session dies)
- Updates display based on context
- Can restart bridge server if it crashes
- Runs silently to minimize token usage

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
curl http://localhost:8081/inputs
```

## Why This Approach?

1. **Simple**: Agent + hooks handle everything automatically
2. **Non-blocking**: Doesn't interfere with typing or tool execution
3. **Immediate**: Real-time visual feedback for all states
4. **Smart**: Full context awareness for button press responses
5. **Clean**: Uses native Claude Code hooks for state management

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

## Starting the T-Keyboard Agent

**IMPORTANT:** This project includes a custom subagent definition that manages the T-Keyboard automatically.

### How to Start the Agent

The `tkeyboard-manager` subagent is defined in `.claude/agents/tkeyboard-manager.md` and is automatically available when working in this project directory.

**To launch it:**
```
Use the tkeyboard-manager subagent to start monitoring
```

**Note:** Custom subagents cannot be invoked directly via the Task tool's `subagent_type` parameter. Instead, use a general-purpose agent and request it to use the custom subagent via natural language.

**What the agent does:**
1. Creates `bridge-server/monitor.sh` - a monitoring script
2. Runs the script in background (completely silent operation)
3. Monitors session health every 2 seconds
4. Auto-terminates if main session dies or session ID changes
5. Restarts bridge server if it crashes (doesn't die)

The agent is configured with:
- **Model:** Haiku (fast, token-efficient for background tasks)
- **Tools:** Bash, Read, Grep (restricted to necessary tools only)
- **Description:** Proactively monitors T-Keyboard system
- **Token efficiency:** Runs silently - zero token waste during monitoring

### Agent Responsibilities

The agent will:
- Start bridge server if not running
- Monitor main session health (auto-terminates if main session dies)
- Set default buttons (Yes/No/Proceed/Help) on startup

### Automatic Thinking State (Hooks)

The keyboard automatically shows thinking state via Claude Code hooks:

**Setup (one-time, via `/hooks` command):**
1. Run `/hooks` in Claude Code
2. Add **PreToolUse** hook:
   - Matcher: `.*` (matches all tools)
   - Command: `~/.claude/hooks/tkeyboard-thinking-start.sh`
3. Add **PostToolUse** hook:
   - Matcher: `.*`
   - Command: `~/.claude/hooks/tkeyboard-thinking-end.sh`
4. Save to project-level settings

**What happens:**
- Before any tool executes → STOP button appears
- After tool completes → Default buttons (Yes/No/Proceed/Help) restore
- Fully automatic, no agent intervention needed

**Hook files** (already in `~/.claude/hooks/`):
- `tkeyboard-thinking-start.sh` - Shows STOP button
- `tkeyboard-thinking-end.sh` - Restores default buttons

### Without the Agent

You can still test the system manually:
- Start bridge: `node bridge-server/agent-bridge.js`
- Update buttons: `curl -X POST http://localhost:8081/update -d '{"buttons":["Yes","No","Proceed","Help"]}'`
- But you won't get automatic health monitoring or default button setup