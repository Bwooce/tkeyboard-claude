# T-Keyboard Integration with Claude Code

## The Challenge

How can the T-Keyboard send real-time input to Claude Code and interrupt ongoing responses?

## Solutions Explored

### ❌ Option 1: Background Agent (Doesn't Work)
- **Tried**: Running an agent via Task tool to monitor inputs
- **Problem**: Agents run in isolation and cannot inject messages into the main conversation
- **Status**: Not viable

### ❌ Option 2: Bash Monitoring Loops (Partial)
- **Tried**: Background bash loops polling `/hook/get-inputs`
- **Problem**: Claude only sees the output when explicitly checking via BashOutput tool
- **Status**: Good for logging, not for real-time interaction

### ✅ Option 3: User Prompt Submit Hook (Recommended)
- **How it works**: Hook runs BEFORE Claude processes each user message
- **Implementation**:
  1. User configures hook in `~/.claude/config.json`
  2. Hook script checks `/hook/get-inputs` for button presses
  3. If button pressed, prepend it to the user's message
  4. Claude sees: "[Keyboard: Yes] <user's actual message>"

**Hook Configuration:**
```json
{
  "hooks": {
    "user-prompt-submit": "~/.claude/hooks/tkeyboard-inject.sh"
  }
}
```

**Hook Script (`~/.claude/hooks/tkeyboard-inject.sh`):**
```bash
#!/bin/bash
# Inject T-Keyboard button presses into user prompts

RESPONSE=$(curl -s http://localhost:8081/hook/get-inputs 2>/dev/null)

if [ $? -eq 0 ] && [ ! -z "$RESPONSE" ]; then
    # Parse JSON to check if there are inputs
    INPUTS=$(echo "$RESPONSE" | jq -r '.inputs')

    if [ "$INPUTS" != "[]" ] && [ "$INPUTS" != "null" ]; then
        # Extract the most recent button text
        BUTTON_TEXT=$(echo "$RESPONSE" | jq -r '.inputs[-1].text')

        if [ ! -z "$BUTTON_TEXT" ] && [ "$BUTTON_TEXT" != "null" ]; then
            # Prepend button press to user message
            echo "[Keyboard: $BUTTON_TEXT]"
        fi
    fi
fi
```

### ✅ Option 4: Tool Call Hooks (For State Updates)
- **Hook**: `pre-tool-call` or `post-tool-call`
- **Use**: Update keyboard state when Claude starts/finishes thinking
- **Example**: Send "thinking" state and STOP button before tool execution

**Hook Configuration:**
```json
{
  "hooks": {
    "pre-tool-call": "curl -X POST http://localhost:8081/hook/update -H 'Content-Type: application/json' -d '{\"event\":\"thinking_start\"}' && curl -X POST http://localhost:8081/update -H 'Content-Type: application/json' -d '{\"buttons\":[\"STOP\",\"\",\"\",\"\"],\"images\":[\"stop.rgb\",\"\",\"\",\"\"]}' 2>/dev/null",
    "post-tool-call": "curl -X POST http://localhost:8081/hook/update -H 'Content-Type: application/json' -d '{\"event\":\"thinking_end\"}' 2>/dev/null"
  }
}
```

### 🔄 Option 5: Hybrid Approach (Best Solution)

Combine multiple hooks for full integration:

```json
{
  "hooks": {
    "user-prompt-submit": "~/.claude/hooks/tkeyboard-inject.sh",
    "pre-tool-call": "~/.claude/hooks/tkeyboard-thinking-start.sh",
    "post-tool-call": "~/.claude/hooks/tkeyboard-thinking-end.sh"
  }
}
```

**Benefits:**
1. Button presses automatically appear in conversation
2. Keyboard shows real-time thinking state
3. Stop button visible when Claude is processing
4. No manual checking required

## Implementation Steps

### Step 1: Create Hook Scripts

```bash
# Create hooks directory
mkdir -p ~/.claude/hooks

# Create injection script
cat > ~/.claude/hooks/tkeyboard-inject.sh << 'EOF'
#!/bin/bash
RESPONSE=$(curl -s http://localhost:8081/hook/get-inputs 2>/dev/null)
if [ $? -eq 0 ] && [ ! -z "$RESPONSE" ]; then
    INPUTS=$(echo "$RESPONSE" | jq -r '.inputs')
    if [ "$INPUTS" != "[]" ] && [ "$INPUTS" != "null" ]; then
        BUTTON_TEXT=$(echo "$RESPONSE" | jq -r '.inputs[-1].text')
        if [ ! -z "$BUTTON_TEXT" ] && [ "$BUTTON_TEXT" != "null" ]; then
            echo "[Keyboard: $BUTTON_TEXT]"
        fi
    fi
fi
EOF

# Create thinking start script
cat > ~/.claude/hooks/tkeyboard-thinking-start.sh << 'EOF'
#!/bin/bash
curl -s -X POST http://localhost:8081/hook/update \
  -H 'Content-Type: application/json' \
  -d '{"event":"thinking_start"}' > /dev/null 2>&1

curl -s -X POST http://localhost:8081/update \
  -H 'Content-Type: application/json' \
  -d '{"buttons":["STOP","","",""],"images":["stop.rgb","","",""]}' > /dev/null 2>&1
EOF

# Create thinking end script
cat > ~/.claude/hooks/tkeyboard-thinking-end.sh << 'EOF'
#!/bin/bash
curl -s -X POST http://localhost:8081/hook/update \
  -H 'Content-Type: application/json' \
  -d '{"event":"thinking_end"}' > /dev/null 2>&1

curl -s -X POST http://localhost:8081/update \
  -H 'Content-Type: application/json' \
  -d '{"buttons":["Yes","No","Continue","Help"]}' > /dev/null 2>&1
EOF

# Make executable
chmod +x ~/.claude/hooks/*.sh
```

### Step 2: Configure Hooks

User must manually edit `~/.claude/config.json` to add:

```json
{
  "hooks": {
    "user-prompt-submit": "~/.claude/hooks/tkeyboard-inject.sh",
    "pre-tool-call": "~/.claude/hooks/tkeyboard-thinking-start.sh",
    "post-tool-call": "~/.claude/hooks/tkeyboard-thinking-end.sh"
  }
}
```

### Step 3: Test

1. Start bridge server: `node bridge-server/agent-bridge.js`
2. Press a button on keyboard
3. Type any message in Claude Code
4. Claude should see: "[Keyboard: ButtonText] your message"

## Stop Button Limitation

**Important**: Even with hooks, the STOP button cannot actually interrupt Claude mid-response because:
- Hooks only run between messages/tool calls
- Once Claude starts generating text, it cannot be interrupted
- The stop button can only affect what happens NEXT

**What STOP can do:**
- Set a flag that Claude checks before next tool call
- Prevent additional tool calls in the same response
- Signal to skip remaining tasks

**What STOP cannot do:**
- Interrupt text generation mid-stream
- Cancel an already-running tool call

## Conclusion

The hybrid hook approach is the most practical solution for T-Keyboard integration with Claude Code. It provides:
- Automatic button press injection
- Real-time state updates
- Context-aware button options
- Minimal latency (hook execution is fast)

The system won't be truly "interrupt-driven" but will feel responsive for most use cases.
