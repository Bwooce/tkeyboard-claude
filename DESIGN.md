# T-Keyboard MCP Integration Design

> **Note:** This document describes the MCP-based architecture (current). The previous bash script + subagent approach has been replaced with MCP server + button-advisor for better integration and dynamic button generation.

## Overview

The T-Keyboard integrates with Claude Code using an **MCP server + AI button advisor architecture**:
- **MCP Server** exposes tools for keyboard context management, runs WebSocket server for ESP32
- **Button Advisor** (AI subagent) analyzes context and determines optimal buttons dynamically
- **Hooks** (PreToolUse/PostToolUse) automatically manage visual state (STOP button during tool execution)
- **Input Daemon** polls for button presses and injects them via AppleScript (essential for blocked input)
- **AppleScript** simulates actual keypresses (works when main conversation is blocked)

Main conversation uses MCP tools to update keyboard context. MCP server launches button-advisor subagent for AI-powered button recommendations.

## MCP Server Architecture

The system uses an **MCP (Model Context Protocol) server** in `mcp-server/`:

- **Type:** Persistent Node.js process (TypeScript compiled)
- **Communication:** stdio transport for MCP, WebSocket for ESP32, HTTP for input daemon
- **Tools Exposed:** update_keyboard_context, set_keyboard_buttons, get_keyboard_status
- **Resources Exposed:** tkeyboard://status, tkeyboard://context
- **Port 8080:** WebSocket server for ESP32
- **Port 8081:** HTTP API for input daemon compatibility

**Key Features:**
- Tools automatically available in Claude Code when MCP server is configured
- Main conversation calls tools natively (discoverable in tool list)
- Launches button-advisor subagent for dynamic button determination
- Generates icons on-demand using existing generate-images.js
- Maintains WebSocket connection to ESP32
- Provides HTTP endpoints for input daemon compatibility
- Type-safe tool parameters with schema validation

**Why MCP vs Bash Scripts:**
- Native tool integration (not curl commands)
- Type-safe parameters with validation
- Standardized protocol (MCP SDK)
- Single persistent process (no bash script proliferation)
- Cleaner error handling (tool responses)
- Better debugging (MCP inspector compatible)

## System Startup and Session Management

**Critical:** The MCP server and input daemon must use the **same session ID** to work together. Session ID mismatches cause the daemon to ignore queued inputs.

### Unified Startup Script (`mcp-server/start-system.sh`)

The startup script ensures session ID alignment:

```bash
./start-system.sh
```

**What it does:**
1. Finds Claude Code PID automatically (searches for running `claude` process)
2. Generates session ID: `tk-{timestamp}-{claude-pid}`
3. Exports `CLAUDE_SESSION_ID` and `CLAUDE_PID` environment variables
4. Starts MCP server (reads env vars)
5. Starts input daemon with same session ID
6. Displays status dashboard with PIDs and log locations
7. Traps Ctrl+C to cleanly stop both processes

**Session ID Configuration:**
```typescript
// mcp-server/src/tkeyboard-server.ts:22
sessionId: process.env.CLAUDE_SESSION_ID || `tk-${Date.now()}-${process.pid}`
```

The MCP server checks for `CLAUDE_SESSION_ID` environment variable first, allowing the startup script to control the session ID.

### Shutdown Script (`mcp-server/stop-system.sh`)

Clean shutdown of all components:
```bash
./stop-system.sh
```

Kills both MCP server and input daemon processes.

### Manual Startup (Not Recommended)

If you need to start components separately:

```bash
# Start MCP server
export CLAUDE_SESSION_ID="tk-$(date +%s)-{YOUR_CLAUDE_PID}"
export CLAUDE_PID="{YOUR_CLAUDE_PID}"
node build/tkeyboard-server.js &

# Start input daemon with SAME session ID
bash ../installation/tkeyboard-input-daemon.sh "$CLAUDE_SESSION_ID" "$CLAUDE_PID" &
```

**Warning:** Manual startup is error-prone. Use `start-system.sh` instead.

## Button Advisor Subagent

The MCP server launches the **button-advisor** subagent (`.claude/agents/button-advisor.md`) for dynamic button decisions:

- **Model:** Haiku (cheap and fast)
- **Tools:** Read, Grep (for context analysis)
- **Input:** Context type + detail from main conversation
- **Output:** JSON with buttons, emojis, and reasoning

The advisor analyzes the specific situation and recommends appropriate buttons, not hardcoded mappings.

## Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                        Claude Code Session                            │
│                         (PID: 2069)                                  │
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────────┐│
│  │  Claude Code Hooks                                              ││
│  │  PreToolUse  → Show STOP button                                ││
│  │  PostToolUse → Show default buttons (Yes/No/Proceed/Help)      ││
│  └─────────────────────────────────────────────────────────────────┘│
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────────┐│
│  │  Terminal (receives AppleScript keypresses)                     ││
│  │  > Yes<CR>         ← AppleScript injection                      ││
│  │  > Proceed<CR>     ← AppleScript injection                      ││
│  │  > <Esc>           ← STOP button (stops generation)             ││
│  └─────────────────────────────────────────────────────────────────┘│
└───────────────────────────────┬───────────────────────────────────────┘
                                │
                                │ tkeyboard-manager subagent starts:
                                ↓
                  ┌─────────────────────────────┐
                  │  Subagent (Haiku)           │
                  │  .claude/agents/            │
                  │   tkeyboard-manager.md      │
                  └──────┬──────────────┬───────┘
                         │              │
            ┌────────────┘              └────────────┐
            ↓                                        ↓
  ┌───────────────────┐                  ┌──────────────────────────┐
  │  Monitor Script   │                  │   Input Daemon           │
  │  (monitor.sh)     │                  │  (Bash)                  │
  │                   │                  │                          │
  │  • Health checks  │                  │  For each button press:  │
  │  • 2s interval    │                  │  • STOP → Esc via        │
  │  • Auto-terminate │                  │    AppleScript           │
  │  • Restart bridge │                  │  • Other → Text+Enter    │
  └───────────────────┘                  │    via AppleScript       │
                                         │                          │
                                         │  Session validation:     │
                                         │  • Matches SESSION_ID    │
                                         │  • Claude PID alive      │
                                         └─────────┬────────────────┘
                                                   │
                                                   │ polls every 100ms
                                                   ↓
  ┌────────────────────┐                  ┌───────────────────┐
  │  T-Keyboard ESP32  │──WebSocket──────>│  Bridge Server    │
  │                    │   port 8080       │  (Node.js)        │
  │  ┌───┐ ┌───┐      │                  │  Port 8080/8081   │
  │  │Yes│ │ No│      │                  │                   │
  │  └───┘ └───┘      │                  │  • WebSocket      │
  │  ┌───┐ ┌───┐      │                  │  • HTTP API       │
  │  │Pro│ │Hlp│      │                  │  • Input queue    │
  │  └───┘ └───┘      │                  │  • Session ID     │
  │                    │                  └───────────────────┘
  │  User presses "Yes"│
  │  ↓                 │                  AppleScript injection:
  │  WebSocket msg:    │                  osascript -e 'tell application
  │  {type:"key_press",│                   "System Events" to keystroke "Yes"'
  │   key:1,           │                  osascript -e 'tell application
  │   text:"Yes"}      │                   "System Events" to key code 36'
  └────────────────────┘                  (36 = Enter key)
```

### Data Flow for Button Press

```
1. User presses "Yes" button on T-Keyboard
         ↓
2. ESP32 sends WebSocket: {"type":"key_press", "key":1, "text":"Yes"}
         ↓
3. Bridge server queues: {key:1, text:"Yes", timestamp:1760836048000}
         ↓
4. Input daemon polls (100ms): GET /inputs
         ↓
5. Daemon validates SESSION_ID matches and Claude PID is alive
         ↓
6. Daemon injects via AppleScript:
   osascript -e 'tell application "System Events" to keystroke "Yes"'
   osascript -e 'tell application "System Events" to key code 36'
         ↓
7. Claude terminal receives actual keypresses: "Yes<Enter>"
         ↓
8. Claude processes as if user typed "Yes" and pressed Enter
         ↓
9. Claude responds in conversation
```

## Session Binding

### Session ID Format
```
tk-{TIMESTAMP}-{CLAUDE_PID}

Example: tk-1760835985-24429
```

### Components

#### 1. Agent Startup Script (`~/.claude/tkeyboard-agent-start.sh`)

**Responsibilities:**
- Auto-detect Claude process PID and TTY
- Generate unique session ID
- Start bridge server with session environment variables
- Start stop daemon bound to session
- Monitor both processes for failures
- Clean up on exit

**TTY Detection Algorithm:**
```bash
# 1. Try to get TTY from parent process
CLAUDE_PID=$(ps -o ppid= -p $$ | tr -d ' ')
TTY_NAME=$(ps -o tty= -p "$CLAUDE_PID" | tr -d ' ')

# 2. Fallback: search for claude process with TTY
if [ "$TTY_NAME" == "??" ]; then
    # Find claude process that has a TTY (not ??)
    CLAUDE_INFO=$(ps aux | grep -E "^\s*$USER\s+[0-9]+.*claude\s*$" | grep -v '??' | head -1)
    CLAUDE_PID=$(echo "$CLAUDE_INFO" | awk '{print $2}')
    TTY_NAME=$(echo "$CLAUDE_INFO" | awk '{print $7}')
fi

# 3. Convert to full path (s005 -> /dev/ttys005)
if [[ "$TTY_NAME" =~ ^s[0-9]+ ]]; then
    TTY_PATH="/dev/tty$TTY_NAME"
fi
```

**Environment Variables:**
- `CLAUDE_SESSION_ID` - Unique session identifier
- `CLAUDE_PID` - Process ID of Claude instance

**Lifecycle:**
```bash
trap cleanup EXIT INT TERM  # Cleanup on exit

cleanup() {
    kill $DAEMON_PID
    kill $BRIDGE_PID
    rm ~/.claude/active-session.json
}
```

#### 2. Bridge Server (`bridge-server/agent-bridge.js`)

**Session Configuration:**
```javascript
const config = {
    wsPort: 8080,
    httpPort: 8081,
    sessionId: process.env.CLAUDE_SESSION_ID || `tk-${Date.now()}-${process.pid}`,
    claudePid: parseInt(process.env.CLAUDE_PID || process.ppid)
};
```

**API Endpoints:**

| Endpoint | Method | Purpose | Response |
|----------|--------|---------|----------|
| `/inputs` | GET | Retrieve queued button presses | `{sessionId, claudePid, inputs: [...]}` |
| `/session/verify` | GET | Check if Claude process is alive | `{sessionId, claudePid, alive: true/false}` |
| `/update` | POST | Update keyboard display | `{success: true}` |
| `/state/update` | POST | Update keyboard state (thinking/idle) | `{success: true}` |
| `/test/button` | POST | Simulate button press (testing) | `{success: true, queued: N}` |
| `/status` | GET | Server status | `{sessionId, claudePid, connected, queueLength, uptime}` |

**Input Queue Format:**
```javascript
{
    key: 1,           // Button number (1-4)
    text: "Yes",      // Button label
    timestamp: 1760835906000
}
```

#### 3. Input Daemon (`~/.claude/tkeyboard-input-daemon.sh`)

**Responsibilities:**
- Poll bridge server every 100ms for button presses
- Validate session ID matches before injecting
- Handle STOP button (Ctrl+C) and normal buttons (text + Enter)
- Auto-exit if Claude process dies

**Validation and Injection Logic:**
```bash
# 1. Check Claude process is alive
if ! kill -0 "$CLAUDE_PID" 2>/dev/null; then
    exit 0  # Claude died, daemon should exit
fi

# 2. Poll bridge server
RESPONSE=$(curl -s http://localhost:8081/inputs)

# 3. Validate session ID
RESPONSE_SESSION=$(echo "$RESPONSE" | jq -r '.sessionId')
if [ "$RESPONSE_SESSION" != "$SESSION_ID" ]; then
    # Wrong session! Log warning and skip
    continue
fi

# 4. Process each button press
for input in inputs; do
    if [ "$text" == "STOP" ]; then
        # STOP button → Send Ctrl+C
        echo -ne "\003" > "$TTY_PATH"
        sleep 1  # Cooldown after interrupt
    else
        # Normal button → Inject text + Enter
        echo -n "$text" > "$TTY_PATH"      # The button text
        echo -ne "\015" > "$TTY_PATH"       # Carriage return (Enter)
        sleep 0.2  # Small cooldown
    fi
done
```

**Button Handling:**

| Button | Text | Action | TTY Output |
|--------|------|--------|------------|
| STOP | "STOP" | Send Ctrl+C | `\003` (ASCII 3) |
| Yes | "Yes" | Inject text + CR | `Yes\r` |
| No | "No" | Inject text + CR | `No\r` |
| Continue | "Continue" | Inject text + CR | `Continue\r` |
| Help | "Help" | Inject text + CR | `Help\r` |

**Safety Features:**
- Session ID validation prevents cross-session interference
- Process liveness check auto-terminates orphaned daemons
- Cooldowns prevent TTY flooding (1s after Ctrl+C, 200ms after text)
- Each button press processed sequentially to avoid race conditions

## AppleScript Keyboard Injection

The system uses **AppleScript Accessibility API** to simulate actual keypresses. This provides truly autonomous keyboard operation without requiring manual typing.

### How AppleScript Injection Works

```bash
# Inject text (e.g., "Yes")
osascript -e 'tell application "System Events" to keystroke "Yes"'

# Inject Enter key (key code 36)
osascript -e 'tell application "System Events" to key code 36'

# Result: Claude Code receives actual keypresses as if user typed "Yes" and pressed Enter
```

**Why AppleScript instead of TIOCSTI:**
- Claude Code's TUI runs in raw mode, bypassing TTY input buffer
- TIOCSTI writes to TTY buffer but Claude never reads from it
- AppleScript simulates actual keyboard events at the system level
- Works with any application including TUI apps in raw mode

### Why AppleScript Instead of Hooks?

**Hooks Limitation:**
- `user-prompt-submit` hook only runs when user manually types something
- Button press alone wouldn't trigger Claude to respond
- Not truly autonomous - still requires manual typing
- Cannot inject input autonomously

**AppleScript Requirements:**
- ✅ Button press alone triggers full interaction
- ✅ No manual typing required
- ✅ Works exactly like user typing + pressing Enter
- ✅ STOP button can send Ctrl+C for immediate interrupt
- ✅ Fully autonomous keyboard operation
- ⚠️ **Requires Accessibility permissions** - Terminal.app must be in System Settings → Privacy & Security → Accessibility
- ✅ Works with TUI apps in raw mode (unlike TIOCSTI)
- ✅ No setuid or sudo required

**Why Not TIOCSTI?**
- ❌ Claude Code's TUI runs in raw mode, bypassing TTY input buffer
- ❌ TIOCSTI successfully writes to TTY buffer but Claude never reads from it
- ❌ Text appears but isn't processed until Enter is manually pressed
- ❌ Requires setuid/sudo permissions
- ✅ AppleScript simulates actual keyboard events - works perfectly

### Optional: State Update Hooks (Not Required)

Users can optionally configure hooks for visual keyboard state updates:

**Configuration (`~/.claude/config.json`):**
```json
{
  "hooks": {
    "pre-tool-call": "curl -X POST http://localhost:8081/update -d '{\"buttons\":[\"STOP\",\"\",\"\",\"\"]}'",
    "post-tool-call": "curl -X POST http://localhost:8081/update -d '{\"buttons\":[\"Yes\",\"No\",\"Continue\",\"Help\"]}'"
  }
}
```

These hooks are optional and only affect keyboard display, not functionality.

## State Management

### Keyboard States

| State | Description | Display | Buttons |
|-------|-------------|---------|---------|
| `idle` | Ready for input | Static display | Context-aware (Yes/No/Continue/Help) |
| `thinking` | Claude processing | Animation | STOP button on key 1 |
| `limit` | Rate limited | Countdown timer | Disabled |
| `error` | Error occurred | Error message | Retry options |

### State Transitions

```
           ┌─────────┐
           │  idle   │◄──────────────────┐
           └────┬────┘                   │
                │                        │
    pre-tool-call hook                post-tool-call hook
                │                        │
                ↓                        │
           ┌──────────┐                 │
           │ thinking │─────────────────┘
           └──────────┘
                │
                │ STOP button pressed
                ↓
           Ctrl+C → Claude TTY
```

## Button Press Flow

### Normal Button (Yes/No/Continue/Help)

```
1. User presses button 2 ("No")
2. ESP32 sends: {"type":"key_press","key":2,"text":"No"}
3. Bridge queues: {key:2, text:"No", timestamp:...}
4. Input daemon polls /inputs
5. Daemon injects "No" + Enter to TTY
6. Claude terminal receives: "No<CR>"
7. Claude processes as if user typed "No" and pressed Enter
```

### STOP Button

```
1. User presses button 1 (STOP)
2. ESP32 sends: {"type":"key_press","key":1,"text":"STOP"}
3. Bridge queues with session ID
4. Input daemon polls (100ms interval)
5. Daemon validates: sessionId matches? ✓
6. Daemon sends Esc via AppleScript:
   osascript -e 'tell application "System Events" to key code 53'
7. Claude receives Esc keypress (stops generation)
8. Current tool execution may complete, then Claude stops
```

## Implementation Status

### ✅ Completed

1. **Subagent Architecture**
   - Custom tkeyboard-manager subagent defined in `.claude/agents/`
   - Automatic Claude PID detection (finds actual process, not helpers)
   - Auto-launches monitor script and input daemon
   - Auto-terminates when main session dies (verified working)
   - Restarts bridge server if it crashes
   - Token-efficient (Haiku model, silent operation)

2. **Hooks System (Working)**
   - PreToolUse hook: Shows STOP button automatically before tool execution
   - PostToolUse hook: Restores default buttons (Yes/No/Proceed/Help) after completion
   - Hooks registered via `/hooks` TUI with matcher pattern `.*`
   - Configuration stored in `.claude/settings.json`
   - Real-time visual feedback of Claude's state

3. **AppleScript Injection (Working)**
   - Input daemon uses AppleScript to simulate actual keypresses
   - Works with TUI apps in raw mode (unlike TIOCSTI)
   - No TTY required, no setuid needed
   - Requires Accessibility permissions for Terminal.app
   - STOP button sends Esc (key code 53) to stop generation
   - Normal buttons send text + Enter (key code 36)

4. **Input Daemon**
   - Polls bridge server every 100ms for button presses
   - Session validation (SESSION_ID and Claude PID checks)
   - Auto-terminates when Claude process dies
   - Handles STOP button (Esc) and normal buttons (text+Enter) separately
   - Started automatically by subagent

5. **Bridge Server Session Support**
   - Session metadata in all API responses
   - `/inputs` endpoint for polling queued button presses
   - `/update` endpoint for changing button display
   - `/status` endpoint for health checks
   - Tracks Claude PID and session ID

### 🚧 TODO - Planned Features

#### 0. BACKGROUND Button During Tool Execution
**Goal:** Add a BACKGROUND button alongside STOP when tools are running

**Problem:**
- Currently during tool execution, only STOP button is shown
- STOP terminates the tool execution (sends Esc)
- No way to "background" Claude and let tools continue while user does other work

**Proposed Solution:**
```javascript
// PreToolUse hook shows both STOP and BACKGROUND
POST /update
{
  "buttons": ["STOP", "BACKGROUND", "", ""],
  "actions": ["STOP", "BACKGROUND", "", ""],
  "images": ["stop.rgb", "background.rgb", "", ""]
}
```

**BACKGROUND Button Behavior:**
- Sends Ctrl+B (key code 11) to background Claude and continue tools
- User can switch to another window/task while Claude works
- PostToolUse hook still fires when tools complete, restoring default buttons
- Input daemon needs to recognize "BACKGROUND" action and send Ctrl+B via AppleScript

**Benefits:**
- Non-destructive alternative to STOP
- Allows long-running tasks to continue unattended
- User can multitask while Claude processes
- Still provides visual feedback when complete

**Implementation:**
- Update PreToolUse hook to show [STOP, BACKGROUND, "", ""]
- Generate background.rgb image (⏸️ or similar icon)
- Update input daemon to recognize "BACKGROUND" action and send Ctrl+B:
  ```bash
  osascript -e 'tell application "System Events" to keystroke "b" using control down'
  ```
- Consider adding notification when backgrounded task completes

**Status:** ✅ COMPLETED - Implemented with Ctrl+B via AppleScript

---

#### 0b. Dynamic Image Generation
**Goal:** Allow agent to generate custom icons on-demand for context-aware buttons

**Implementation:**
- Added POST `/generate-image` endpoint to bridge server
- Takes `emoji` and/or `text` parameters
- Generates hash-based filenames (MD5 of parameters) for automatic caching
- Same parameters always generate same filename (cache-friendly)
- Returns `{success, filename, cached}` JSON response

**Usage Example:**
```bash
# Agent generates commit icon
curl -X POST http://localhost:8081/generate-image \
  -d '{"emoji":"💾"}'
→ {"success":true,"filename":"a2fc31d8.rgb","cached":false}

# Agent updates keyboard
curl -X POST http://localhost:8081/update \
  -d '{"buttons":["Commit","Push","Skip","Help"],"images":["a2fc31d8.rgb","...","","help.rgb"]}'
```

**Benefits:**
- Agent can create icons for any context (git, debug, tests, errors, etc.)
- No need to pre-generate all possible icons
- Automatic caching prevents regeneration
- ESP32 downloads via existing GET `/images/{filename}` endpoint

**Status:** ✅ COMPLETED - Working with caching

---

#### 1. Expand Subagent with AGENT_PROMPT.md Content
**Goal:** Port additional context-aware button examples and logic from AGENT_PROMPT.md into the subagent

**Current State:**
- Subagent (`.claude/agents/tkeyboard-manager.md`) handles basic setup and health monitoring
- AGENT_PROMPT.md contains more detailed examples of context-aware button updates
- Need to merge the best parts of AGENT_PROMPT.md into the subagent definition

**Content to Port:**
- Context-aware button suggestions based on conversation state
- Image generation examples and instructions
- Rate limit detection and handling
- Button update examples for different scenarios
- Error state handling
- Multi-step task detection

**Benefits:**
- Single source of truth (subagent becomes canonical)
- Better context awareness (buttons change based on what Claude is doing)
- Can eventually deprecate AGENT_PROMPT.md
- Richer functionality without manual intervention

**Status:** TODO - AGENT_PROMPT.md still exists with additional content not in subagent

#### 2. Claude TUI Prompt Detection
**Goal:** Automatically detect and handle Claude's standard confirmation prompts

Common Claude prompts to detect:
- **Yes/Always/No prompts** - Permission requests for tools
- **Overwrite/Cancel prompts** - File operation confirmations
- **Continue/Stop prompts** - Multi-step operation confirmations

**Implementation Strategy:**
```javascript
// Pattern matching for prompt detection
const CLAUDE_PROMPTS = {
  yesAlwaysNo: /\[Yes\/Always\/No\]/i,
  overwriteCancel: /\[Overwrite\/Cancel\]/i,
  continueStop: /\[Continue\/Stop\]/i
};

// Auto-update keyboard when detected
if (lastClaudeOutput.match(CLAUDE_PROMPTS.yesAlwaysNo)) {
  updateKeyboard({
    buttons: ["Yes", "Always", "No", "Help"],
    images: ["yes.rgb", "always.rgb", "no.rgb", "help.rgb"]
  });
}
```

**Detection Methods:**
- Parse Claude's last output for prompt patterns
- Monitor for tool-use permission requests
- Detect question patterns with limited options
- Track multi-step task progress

**Benefits:**
- Eliminates need to manually type responses
- Faster workflow - press button instead of typing
- Consistent UX - buttons always match expected inputs
- Visual feedback - images clearly show options

#### 3. Image Preference Strategy
**Goal:** Use images by default where they provide clear visual recognition benefits

**When to Use Images:**
- ✅ **Standard prompts** - Yes/No/Always/Continue/Stop (universal symbols)
- ✅ **Actions** - Run/Test/Debug/Deploy (recognizable icons)
- ✅ **Navigation** - Back/Forward/Home/Exit (directional arrows)
- ✅ **Status** - Success/Error/Warning (color-coded indicators)
- ❌ **Complex text** - Long explanations or context-specific options
- ❌ **Numbers/IDs** - Specific values better shown as text

**Image Categories:**

| Category | Example Buttons | When to Use |
|----------|----------------|-------------|
| **Confirmations** | ✅ Yes, ❌ No, ⚠️ Always | Always use images - universal symbols |
| **Actions** | ▶️ Run, 🧪 Test, 🐛 Debug | When action is well-known |
| **File Ops** | 💾 Save, 🗑️ Delete, 📋 Copy | Always use images - clear icons |
| **Navigation** | ⬅️ Back, ➡️ Next, 🏠 Home | Always use images - directional |
| **Git** | 💾 Commit, 🔀 Merge, 📤 Push | When user knows git workflow |
| **Custom** | Project-specific | Only if icon is intuitive |

**Image Generation Guidelines:**
```bash
# Emoji-based (preferred for universal symbols)
node generate-images.js --emoji "✅" --name yes
node generate-images.js --emoji "❌" --name no
node generate-images.js --emoji "⚠️" --name warning

# Text-based (for custom labels)
node generate-images.js --text "RUN" --color "#00FF00" --name run
node generate-images.js --text "DBG" --color "#FF0000" --name debug

# Combined (text on colored background)
node generate-images.js --text "Y" --emoji "✅" --name yes_alt
```

**Fallback Strategy:**
- If image generation fails → use text-only button
- If image cache is full → prune least-used images
- If emoji not supported → use text abbreviation
- Always provide `action` text alongside image for accessibility

#### 4. Display vs Action Text Separation
**Goal:** Support images or short labels with detailed action text

**⚠️ CRITICAL: Action Text Behavior**

The `action` field is **literally injected to the terminal** as if the user typed it. Choose action text based on context:

**For Claude Code Built-in Prompts (e.g., `[Yes/Always/No]`):**
- ✅ **Use SHORT exact answers:** `actions: ["Yes", "Always", "No"]`
- ❌ **DON'T use verbose text:** `actions: ["Yes, I agree to proceed", ...]` ← Creates duplicate messages!

**For Conversational Input (custom questions):**
- ✅ **Use verbose descriptive text:** `actions: ["Please explain this in more detail"]`
- ✅ **Full sentences are appropriate:** `actions: ["Show me the error logs from yesterday"]`

**Use Cases:**

| Display | Action Text | Context | Result |
|---------|-------------|---------|--------|
| ✅ (yes icon) | "Yes" | Claude prompt `[Yes/Always/No]` | Answers prompt cleanly |
| ✅ (yes icon) | "Yes, I agree to overwrite the file" | Custom question | Creates conversational message |
| "1" (short) | "1" | Claude prompt `[1/2/3]` | Selects option 1 |
| "1" (short) | "Select option 1: Install dependencies" | Custom menu | Sends detailed instruction |
| 🛑 (image) | "STOP" | Interrupt signal | Sends Ctrl+C (handled specially) |

**ESP32 Implementation:**
```cpp
struct KeyOption {
    String text;        // Display text or label (shown on screen)
    String action;      // Action text (sent when pressed)
    String imagePath;   // Optional image (if present, shown instead of text)
    uint32_t color;     // Background color
    bool hasImage;      // Whether to display image
};
```

**Bridge Server API:**
```javascript
// Example: Image buttons with detailed actions
POST /update
{
  "buttons": ["🛑", "✅", "❌", "ℹ️"],
  "actions": [
    "Stop and wait for instructions",
    "Yes, proceed with overwrite",
    "No, cancel the operation",
    "Show me more information about this operation"
  ],
  "images": ["stop.rgb", "yes.rgb", "no.rgb", "info.rgb"]
}
```

**Benefits:**
- Compact visual display (icons/emojis)
- Rich contextual actions (full sentences)
- Accessibility (screen readers use action text)
- Flexibility (same icon, different actions)

#### 5. Context-Aware Button Updates
**Goal:** Automatically suggest relevant buttons based on conversation state

**Context Detection:**
```javascript
const contextPatterns = {
  error: /error|fail|exception|crash/i,
  question: /\?$|should I|would you/i,
  multiStep: /next step|continue|proceeding/i,
  codeReview: /review|refactor|optimize/i,
  gitOps: /commit|push|pull|merge/i
};

function detectContext(lastMessages) {
  if (containsError(lastMessages)) return 'error';
  if (endsWithQuestion(lastMessages)) return 'question';
  if (inMultiStepTask(lastMessages)) return 'multiStep';
  // ...
}
```

**Context → Button Mappings:**
```javascript
const contextButtons = {
  error: {
    buttons: ["Debug", "Retry", "Skip", "Help"],
    images: ["debug.rgb", "retry.rgb", "skip.rgb", "help.rgb"],
    actions: [
      "Show me debug information",
      "Retry the failed operation",
      "Skip this step and continue",
      "Explain what went wrong"
    ]
  },
  question: {
    buttons: ["Yes", "No", "Maybe", "More Info"],
    images: ["yes.rgb", "no.rgb", "maybe.rgb", "info.rgb"]
  },
  claudePrompt: {
    buttons: ["Yes", "Always", "No", "Help"],
    images: ["yes.rgb", "always.rgb", "no.rgb", "help.rgb"]
  }
  // ...
};
```

#### 6. State Synchronization
- ❌ Rate limit state not automatically detected (bridge monitors agent polling)
- ❌ Error state not propagated to keyboard automatically
- ❌ No "task complete" notification with success/failure summary

#### 7. Advanced Interrupt Handling
- ✅ Esc via AppleScript (STOP button - implemented)
- ❌ Ctrl+B via AppleScript (BACKGROUND button - not yet implemented)
- ❌ Graceful task cancellation (save progress before stopping)
- ❌ Resume after interrupt (restore state and continue)

#### 8. Multi-Session Management
- ❌ Session discovery (list active sessions)
- ❌ Session switching on keyboard (select active Claude window)
- ❌ Session persistence across restarts
- ❌ Multiple bridge servers on different ports

#### 9. ESP32 Session Awareness
- ❌ ESP32 doesn't store session ID (only bridge knows)
- ❌ No session mismatch detection on keyboard
- ❌ No visual feedback for session binding status
- ❌ No "reconnecting to session..." state

#### 10. Testing & Debugging
- ✅ `/test/button` endpoint for simulation
- ❌ No integration tests for session binding
- ❌ No error recovery tests (network loss, daemon crash, etc.)
- ❌ No visual regression tests for image generation

## Future Enhancements

### 1. Smart Button Suggestions

Claude could analyze conversation context and suggest relevant buttons:

```javascript
// Example: After asking a question
if (lastMessageWasQuestion) {
    updateButtons(["Yes", "No", "Maybe", "Help"]);
}

// Example: During multi-step task
if (taskInProgress) {
    updateButtons(["Continue", "Skip", "Stop", "Help"]);
}
```

### 2. Session Persistence

Save session state to allow reconnection after restart:

```json
{
  "sessionId": "tk-1760835985-24429",
  "created": "2025-10-19T01:05:00Z",
  "lastActive": "2025-10-19T01:15:30Z",
  "conversationId": "conv_abc123",
  "state": "idle"
}
```

### 3. Visual Session Feedback

Display session info on key 4:
```
┌─────────┐
│ Session │
│ #24429  │
│ Active  │
└─────────┘
```

### 4. Graceful Interrupt

Instead of hard Ctrl+C, implement graceful cancellation:

```javascript
// Set flag for Claude to check
process.env.STOP_REQUESTED = 'true';

// Claude checks between operations
if (process.env.STOP_REQUESTED === 'true') {
    // Save progress, cleanup, then exit
}
```

## Testing Guide

### Test 1: Session Binding

```bash
# Start session
~/.claude/tkeyboard-agent-start.sh

# Verify session ID
curl -s http://localhost:8081/status | jq '.sessionId, .claudePid'

# Check daemon logs
tail -f /tmp/tkeyboard-daemon-tk-*.log
```

### Test 2: STOP Button

```bash
# Simulate STOP press
curl -X POST http://localhost:8081/test/button \
  -H 'Content-Type: application/json' \
  -d '{"key":1,"text":"STOP"}'

# Check daemon response
tail -5 /tmp/tkeyboard-daemon-tk-*.log
# Expected: "[Stop Daemon] STOP button pressed! Sending Ctrl+C..."
```

### Test 3: Normal Button Press

```bash
# Simulate "Yes" press
curl -X POST http://localhost:8081/test/button \
  -H 'Content-Type: application/json' \
  -d '{"key":1,"text":"Yes"}'

# Check queue
curl -s http://localhost:8081/inputs | jq '.inputs'
# Expected: [{"key":1,"text":"Yes","timestamp":...}]
```

### Test 4: Thinking State Hooks (Optional)

```bash
# Test thinking hooks
~/.claude/hooks/tkeyboard-thinking-start.sh
~/.claude/hooks/tkeyboard-thinking-end.sh
# Check keyboard display updates to STOP and back to defaults
```

### Test 5: Multi-Session Safety

```bash
# Start first session in window 1
~/.claude/tkeyboard-agent-start.sh

# Start second session in window 2
~/.claude/tkeyboard-agent-start.sh
# Expected: ERROR (port already in use)

# Future: Should start on different ports and allow session switching
```

## LIMITATIONS - Current Constraints

### 1. Claude Code's Interrupt Response Behavior

**Constraint:** Claude Code may not respond immediately to Ctrl+C in all situations

**Details:**
- ✅ TTY injection is instant: Ctrl+C (`\003`) written to TTY immediately
- ⚠️ Claude's response timing varies by what it's doing
- Claude Code itself determines when to honor the interrupt signal

**Behavior:**
```
User presses STOP
↓
Daemon injects Ctrl+C to TTY instantly
↓
Claude Code receives interrupt signal
↓
Claude may complete current operation before responding:
  - Text streaming: Usually stops quickly
  - Tool execution: May finish current tool
  - External processes: Waits for process completion
↓
Claude shows "Interrupted" message
```

**Impact:**
- STOP button works immediately for keyboard/daemon/bridge
- Apparent delay is Claude Code's interrupt handling, not our system
- Long-running external processes may complete before interrupt
- This is Claude Code behavior, not a keyboard limitation

**Note:**
- Our TTY injection mechanism has no timing constraints
- The delay (if any) is entirely on Claude Code's side
- Same behavior as pressing Ctrl+C manually in terminal

### 2. Single Session Per Machine

**Constraint:** Only one bridge server instance can run at a time

**Details:**
- Bridge binds to fixed ports 8080 (WebSocket) and 8081 (HTTP)
- Multiple Claude windows cannot each have their own keyboard
- Starting second session fails with "port already in use"

**Impact:**
- Can only control one Claude instance at a time
- Multiple users on same machine interfere
- Switching windows requires stopping/restarting bridge

**Workaround (not implemented):**
- Dynamic port allocation (8080-8089)
- Session discovery via mDNS
- Keyboard UI to switch active session

### 3. TTY-Based Injection Limitations

**Constraint:** Direct TTY writing has platform and permission dependencies

**Details:**
- Requires TTY device to be writable by current user
- Doesn't work in all terminal emulators
- Some environments don't expose TTY to scripts (Docker, systemd, etc.)
- macOS specific - Linux paths differ (`/dev/pts/N` vs `/dev/ttyN`)

**Failure Scenarios:**
```bash
# TTY not available
tty: not a tty

# TTY not writable
-bash: /dev/ttys005: Permission denied

# Wrong TTY (headless process)
ps -o tty= -p 12345
??
```

**Impact:**
- May not work in all environments
- Headless Claude processes cannot be controlled
- Docker containers need special configuration

**Workaround:**
- Fallback to SIGINT if TTY write fails (not implemented)
- Permissions setup in installation script
- Document supported environments

### 4. No Persistence or State Recovery

**Constraint:** All session state is in-memory only

**Details:**
- Session ID lost on bridge restart
- Button press history not saved
- No conversation context preservation
- Keyboard must reconnect after bridge restart

**Impact:**
```
Bridge crashes or restarts
↓
Session ID changes
↓
Input daemon still has old session ID
↓
Button presses ignored (session mismatch)
↓
Must restart entire session
```

**Workaround (not implemented):**
- Save session state to `~/.claude/session-state.json`
- Restore session ID on bridge restart
- Persist button press queue to disk

### 5. ESP32 Session Awareness Gap

**Constraint:** Keyboard firmware is session-agnostic

**Details:**
- ESP32 doesn't know which Claude session it's bound to
- No validation of session ID on keyboard side
- Bridge could restart with new session, ESP32 doesn't know
- No feedback if bound Claude process dies

**Scenarios:**
```
Scenario 1: Silent session change
─────────────────────────────────
Claude session A (PID 100) dies
↓
User starts Claude session B (PID 200)
↓
Bridge assigns new SESSION_ID
↓
ESP32 still connected, sends button press
↓
Input daemon rejects (session mismatch)
↓
User confused - button doesn't work

Scenario 2: Wrong session injection
────────────────────────────────────
Session A running with SESSION_ID=tk-100
↓
Bridge crashes, restarts
↓
Session B starts with SESSION_ID=tk-200
↓
ESP32 sends button press (doesn't know session changed)
↓
Could inject to wrong Claude window!
```

**Impact:**
- Silent failures (button press ignored)
- Potential wrong-session injection
- No visual feedback of session state
- User doesn't know which Claude window is active

**Workaround (not implemented):**
- Send session metadata to ESP32
- Display session ID on 4th button
- ESP32 validates session on button press
- Show "Session Lost - Reconnect" state

### 6. Rate Limit Detection Strategies

**Current Implementation:** Dual detection - both active and passive

**Active Detection (Preferred):**
```
Agent hits rate limit
↓
Agent receives error response with retry-after time
↓
Agent immediately POSTs to /state/update:
  {"event":"rate_limit","state":{"countdown":60}}
↓
Bridge forwards to keyboard
↓
Keyboard shows accurate countdown
```

**Passive Detection (Fallback):**
```
Agent hits rate limit but doesn't notify
↓
Agent stops polling (blocked)
↓
10 seconds pass with no poll
↓
Bridge assumes rate limit
↓
Bridge sends {"state":"limit"} to keyboard
↓
Keyboard shows generic "rate limited" message
```

**Best Practice:**
- Agent should POST rate limit info when it gets the error
- Allows accurate countdown display on keyboard
- Passive detection is fallback for when agent can't communicate

**Implementation Note:**
- The agent can actively communicate rate limit duration in its last message before being blocked
- This provides accurate timing instead of relying on passive timeout detection
- See AGENT_PROMPT.md for instructions on POSTing rate limit events

### 7. Action Text Size Limits

**Constraint:** ESP32 has limited memory for button action text

**Details:**
- `String` type in ESP32 dynamically allocated
- Long action text (paragraphs) could cause memory issues
- No hard limit, but realistically ~200-300 chars per button
- 4 buttons × 300 chars = ~1.2KB (manageable)

**Recommended Limits:**
- Display text: 1-20 characters
- Action text: 10-200 characters
- Image path: 20 characters
- Total per button set: < 2KB

**Impact:**
- Very long action text could crash ESP32
- No validation or truncation in bridge server
- Silent failure (ESP32 resets, no error message)

**Workaround:**
- Validate action text length in bridge server
- Truncate with "..." if too long
- Add memory monitoring to ESP32 firmware

### 8. Image Caching Limitations

**Constraint:** SPIFFS has limited space (~13MB) for image cache

**Details:**
- Each 128×128 RGB565 image = 32,768 bytes (~32KB)
- SPIFFS partition = ~13MB
- Theoretical max: 13MB / 32KB ≈ 400 images
- Practical limit: ~300 images (accounting for filesystem overhead)

**Cache Behavior:**
```
1. New image requested
2. Check SPIFFS cache
3. If not cached:
   a. Download from bridge server
   b. Save to SPIFFS
   c. If SPIFFS full → error (no eviction)
4. Load from SPIFFS and display
```

**Impact:**
- Eventually cache fills up
- No LRU eviction or cache management
- Must manually clear cache or re-flash ESP32
- No warning when cache is nearly full

**Workaround (not implemented):**
- Implement LRU cache eviction
- Add `/images/clear-cache` command
- Monitor cache usage, warn at 80%
- Reuse common images (yes.rgb, no.rgb, etc.)

### 9. No Claude TUI Prompt Auto-Detection Yet

**Constraint:** Agent must manually detect and respond to Claude prompts

**Current Behavior:**
- Claude shows "[Yes/Always/No]"
- Agent must parse terminal output
- Agent must recognize prompt pattern
- Agent must update keyboard manually

**Not Implemented:**
- Automatic prompt detection
- Auto-updating keyboard buttons
- Standard prompt library

**Impact:**
- Requires custom agent logic for each prompt type
- Easy to miss prompts if agent doesn't parse output
- Inconsistent UX across different prompts

### 10. Network Dependency

**Constraint:** Keyboard requires WiFi and bridge server to function

**Single Point of Failure:**
```
ESP32 ──WiFi──> Bridge Server ──HTTP──> Claude Agent
```

**Failure Modes:**
- WiFi disconnects → ESP32 reconnects automatically
- Bridge crashes → ESP32 shows "Disconnected"
- Agent stops → Bridge detects via polling timeout
- Claude dies → Input daemon exits

**No Offline Mode:**
- Cannot use keyboard without bridge server
- Cannot cache button configurations locally
- Cannot operate standalone

**Impact:**
- Any network issue breaks functionality
- Bridge server is critical infrastructure
- Debugging requires checking multiple components

## Security Considerations

1. **Process Isolation**
   - Session ID validation prevents cross-session interference
   - PID verification ensures target process exists
   - TTY path validation prevents arbitrary file writes

2. **Input Sanitization**
   - Button text is user-controlled (from ESP32)
   - Should validate/sanitize before injection
   - Potential for command injection via malicious button labels

3. **Local-Only Access**
   - Bridge server binds to localhost only
   - WebSocket should validate client origin
   - No authentication on HTTP endpoints (local only)

## File Reference

### Created Files

| File | Purpose | Lines |
|------|---------|-------|
| `~/.claude/tkeyboard-agent-start.sh` | Session startup orchestration | ~150 |
| `~/.claude/tkeyboard-input-daemon.sh` | TTY injection daemon (all buttons) | ~100 |
| `~/.claude/hooks/tkeyboard-thinking-start.sh` | Pre-tool-call state update (optional) | ~15 |
| `~/.claude/hooks/tkeyboard-thinking-end.sh` | Post-tool-call state update (optional) | ~15 |

### Modified Files

| File | Changes | Lines Modified |
|------|---------|----------------|
| `bridge-server/agent-bridge.js` | Session support, API endpoints | ~380 |
| `arduino/TKeyboardClaude/TKeyboardClaude.ino` | ESP32 firmware (not modified by design system) | N/A |

### Log Files

| File | Contents |
|------|----------|
| `/tmp/tkeyboard-bridge-tk-{SESSION_ID}.log` | Bridge server output |
| `/tmp/tkeyboard-daemon-tk-{SESSION_ID}.log` | Stop daemon output |
| `~/.claude/active-session.json` | Current session metadata |

## Troubleshooting

### Daemon Dies Immediately

**Symptom:** `[Monitor] ERROR: Stop daemon died!`

**Cause:** TTY detection failed (returned "not a tty")

**Solution:** Check TTY detection logic in startup script

```bash
# Debug TTY detection
ps -o pid,tty,command -p $(pgrep claude)
```

### STOP Button Doesn't Work

**Symptom:** Button press logged but no interrupt

**Check:**
1. Daemon logs: `tail -f /tmp/tkeyboard-daemon-tk-*.log`
2. Session ID match: `curl -s http://localhost:8081/status | jq .sessionId`
3. TTY permissions: `ls -la /dev/ttys005`

### Buttons Not Appearing

**Symptom:** Hook runs but `[Keyboard: ...]` not shown

**Check:**
1. Hook permissions: `ls -la ~/.claude/hooks/*.sh`
2. Hook output: Run manually to see errors
3. Queue status: `curl -s http://localhost:8081/hook/get-inputs | jq .`

### Wrong Session Interrupted

**Symptom:** Different Claude window gets interrupted

**Cause:** Session ID mismatch or port collision

**Solution:** Check active sessions and PIDs

```bash
# Find all claude processes
ps aux | grep claude | grep -v grep

# Check which TTY each is on
ps -o pid,tty,command -p $(pgrep claude)
```
