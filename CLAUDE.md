# T-Keyboard-S3 Claude Code Controller - Technical Context

## T-Keyboard Context Management - REQUIRED

**CRITICAL:** This project has an MCP server (tkeyboard) that manages the T-Keyboard hardware.

### You MUST Proactively Update Keyboard Context

After any significant event or before showing prompts to the user, you MUST call the MCP tool `update_keyboard_context`.

**When to update:**
- After git operations → context="git_operations"
- After detecting errors → context="debugging"
- Running tests → context="testing"
- Before Yes/No questions → context="question_yesno"
- Before multiple choice → context="question_choice"
- File operations → context="file_operations"
- Default/unclear → context="default"

**Timing is CRITICAL:**
- Update keyboard BEFORE finishing your response
- Buttons must be ready when user sees the prompt
- Include specific details in the "detail" parameter

**This is not optional** - keyboard UX depends on these updates.

## Architecture: MCP + Hooks

This project uses an **MCP server** for keyboard management and **hooks** for thinking state.

### The Flow
```
Main Conversation → MCP Tools → MCP Server → Button Advisor (AI) → ESP32
                                     ↓
                             Input queue for daemon
                                     ↑
Button Press: T-Keyboard → MCP Server → Input Daemon → AppleScript → Terminal
Thinking State: PreToolUse hook → STOP button → PostToolUse hook → Default buttons
```

## Key Components

### 1. ESP32 Firmware (`arduino/TKeyboardClaude/`)
- WebSocket client connects to bridge server
- WiFi configuration via AP mode (192.168.4.1)
- 4x 128x128 displays with individual CS pins
- RGB LEDs for status (GPIO 11)
- SPIFFS for image caching (~13MB partition)

### 2. MCP Server (`mcp-server/`)
- WebSocket server on port 8080 for ESP32 connection
- HTTP API on port 8081 for input daemon compatibility
- Exposes MCP tools for keyboard context management
- Launches button-advisor subagent for dynamic button decisions
- Generates icons on-demand using existing generate-images.js
- Queues keyboard inputs for input daemon

### 3. Button Advisor Subagent (`.claude/agents/button-advisor.md`)
- Haiku-powered AI that analyzes context
- Determines optimal buttons for each situation
- Returns button labels, emojis, and reasoning
- Cheap and fast (token-efficient)

### 4. Input Daemon (`installation/tkeyboard-input-daemon.sh`)
- **ESSENTIAL** for injecting input when main conversation is blocked
- Polls MCP server's `/inputs` endpoint every 100ms
- Uses AppleScript to inject keypresses into terminal
- Handles STOP (Esc), BACKGROUND (Ctrl+B), and text buttons

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

## MCP Tools Available

The main conversation can use these MCP tools:

**`update_keyboard_context(context, detail?)`**
- Updates keyboard buttons based on work context
- Launches button-advisor subagent for AI-powered recommendations
- Generates icons dynamically as needed
- Context types: git_operations, debugging, testing, question_yesno, question_choice, file_operations, default

**`set_keyboard_buttons(buttons, actions?, images?)`**
- Directly set specific buttons (bypass AI advisor)
- Use for explicit control when needed

**`get_keyboard_status()`**
- Query keyboard connection and current button state

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
# Check MCP server status
curl -s http://localhost:8081/status | jq

# Simulate button press (will inject into terminal)
curl -X POST http://localhost:8081/test/button \
  -H "Content-Type: application/json" \
  -d '{"key":1,"text":"Yes"}'

# Check input queue (used by daemon)
curl -s http://localhost:8081/inputs | jq
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
├── arduino/TKeyboardClaude/       # ESP32 firmware
├── mcp-server/                    # MCP server (NEW)
│   ├── src/
│   │   ├── tkeyboard-server.ts    # Main MCP server
│   │   └── icon-generator.ts      # Icon generation wrapper
│   └── package.json
├── .claude/agents/
│   └── button-advisor.md          # AI button advisor subagent
├── installation/
│   └── tkeyboard-input-daemon.sh  # AppleScript input injection
├── bridge-server/
│   └── generate-images.js         # Icon generator (used by MCP)
├── images/cache/                  # Generated icon cache
└── README.md                      # User documentation
```

## Starting the T-Keyboard System

**CRITICAL:** The MCP server and input daemon must use the **same session ID**. Session mismatches prevent button presses from working.

### Recommended: Unified Startup Script

**First time setup:**
```bash
cd mcp-server
npm install
npm run build
```

**Start the system (MCP server + input daemon):**
```bash
cd mcp-server
./start-system.sh
```

This script:
- Automatically finds Claude Code PID
- Generates a shared session ID
- Starts MCP server with that session ID
- Starts input daemon with matching session ID
- Displays status dashboard

**Stop the system:**
```bash
cd mcp-server
./stop-system.sh
```

### If You Need to Manually Manage Components

**Check if system is running:**
```bash
# Check MCP server
curl -s http://localhost:8081/status | jq

# Check input daemon
ps aux | grep tkeyboard-input-daemon
```

**Stop components manually:**
```bash
# Kill MCP server
pkill -f "node build/tkeyboard-server.js"

# Kill input daemon
pkill -f "tkeyboard-input-daemon.sh"
```

### MCP Configuration in Claude Code

The MCP server should be configured in Claude Code's MCP settings. Once configured, the tkeyboard tools will be automatically available in all conversations.

**Location:** Settings → MCP → Add Server
**Server name:** tkeyboard
**Command:** `node /Users/bruce/Documents/Arduino/tkeyboard-claude/mcp-server/build/tkeyboard-server.js`

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

## Testing Without Hardware

You can test the system without ESP32 hardware connected:

**Simulate a button press:**
```bash
curl -X POST http://localhost:8081/test/button \
  -H "Content-Type: application/json" \
  -d '{"key":1,"text":"Yes"}'
```

This will inject "Yes" into your Claude Code terminal via the input daemon.

**Check system status:**
```bash
# Server status (shows if ESP32 is connected)
curl -s http://localhost:8081/status | jq

# Current button configuration
curl -s http://localhost:8081/inputs | jq
```