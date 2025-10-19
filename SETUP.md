# T-Keyboard Setup Guide

## Prerequisites

- ESP32-S3 T-Keyboard hardware
- macOS (tested on macOS 15.x)
- Node.js installed
- Arduino IDE or arduino-cli

## Hardware Setup

1. **Flash ESP32 Firmware**
   - Open `arduino/TKeyboardClaude/TKeyboardClaude.ino` in Arduino IDE
   - Board: ESP32S3 Dev Module
   - PSRAM: OPI PSRAM (critical!)
   - Upload to T-Keyboard

2. **Configure WiFi**
   - Connect to T-Keyboard's serial port (115200 baud)
   - Send: `WIFI:YourSSID:YourPassword`
   - Send: `HOST:your-computer-ip`
   - Send: `PORT:8080`

## Software Setup

### 1. Install Dependencies

```bash
cd tkeyboard-claude/bridge-server
npm install
```

### 2. Set Up AppleScript Keyboard Injection

The T-Keyboard uses AppleScript Accessibility API to simulate actual keypresses.

**Grant Accessibility permissions:**

1. Open **System Settings → Privacy & Security → Accessibility**
2. Click the **+** button
3. Add **Terminal.app** (or your terminal emulator)
4. Enable the checkbox

**Verify it works:**

```bash
# This should type "Test" in your terminal
osascript -e 'tell application "System Events" to keystroke "Test"'

# This should press Enter
osascript -e 'tell application "System Events" to key code 36'
```

If you see "osascript is not allowed to send keystrokes", verify Terminal.app is enabled in Accessibility settings.

### 3. Generate Button Images

```bash
cd bridge-server
npm run generate-images
```

This creates RGB565 images for common button icons (yes, no, help, stop, etc).

## Running the System

### Automatic Startup (Recommended)

Start everything with one command:

```bash
~/.claude/tkeyboard-agent-start.sh
```

This will:
- Auto-detect your Claude session PID and TTY
- Start the bridge server
- Start the input daemon (polls keyboard, injects to TTY)
- Display session information

### Manual Startup

If you need to run components separately:

**1. Start Bridge Server:**

```bash
cd bridge-server
node agent-bridge.js
```

**2. Start Input Daemon:**

```bash
~/.claude/tkeyboard-input-daemon.sh SESSION_ID CLAUDE_PID TTY_PATH
```

Example:
```bash
~/.claude/tkeyboard-input-daemon.sh tk-$(date +%s)-$$ $$ /dev/ttys005
```

## Testing

### Test Bridge Server

```bash
curl http://localhost:8081/status
```

Should return keyboard connection status and queue info.

### Test Button Press

```bash
curl -X POST http://localhost:8081/test/button \
  -H 'Content-Type: application/json' \
  -d '{"key":1,"text":"Yes"}'
```

Then check the queue:
```bash
curl http://localhost:8081/inputs
```

### Test TTY Injection

```bash
# Should type "Hello" and press Enter in your terminal
bridge-server/tty-inject /dev/$(tty | sed 's|/dev/||') "Hello"
```

## Troubleshooting

### "osascript is not allowed to send keystrokes"

Terminal.app needs Accessibility permissions:

1. Open **System Settings → Privacy & Security → Accessibility**
2. Find **Terminal.app** in the list
3. Enable the checkbox
4. You may need to restart Terminal.app

Test with:
```bash
osascript -e 'tell application "System Events" to keystroke "Test"'
```

### ESP32 Can't Connect to Bridge

1. Check your computer's IP address: `ifconfig | grep "inet "`
2. Make sure firewall allows port 8080
3. Verify WiFi credentials on keyboard: send `STATUS` via serial

### Images Not Displaying

1. Check SPIFFS partition: Serial should show "SPIFFS Ready"
2. Verify partition scheme in Arduino IDE: "Custom (3MB app, 13MB SPIFFS)"
3. Regenerate images: `npm run generate-images`

### Keyboard Shows "Rate Limit"

The input daemon isn't running or stopped polling. Restart with:

```bash
~/.claude/tkeyboard-agent-start.sh
```

### Keyboard Injection Not Working

1. Verify daemon is running: `ps aux | grep tkeyboard-input-daemon`
2. Check daemon logs: `tail -f /tmp/tkeyboard-daemon-*.log`
3. Verify Accessibility permissions are granted to Terminal.app
4. Test manually: `osascript -e 'tell application "System Events" to keystroke "Test"'`
5. Check for error messages about osascript permissions

## File Locations

- Bridge server: `tkeyboard-claude/bridge-server/agent-bridge.js`
- Input daemon: `~/.claude/tkeyboard-input-daemon.sh`
- Startup script: `~/.claude/tkeyboard-agent-start.sh`
- Session info: `~/.claude/active-session.json`
- Logs: `/tmp/tkeyboard-*.log`
- Thinking state hooks (optional): `~/.claude/hooks/tkeyboard-thinking-*.sh`

## Claude Code Tool Permissions (For Agent Mode)

If using an agent to manage keyboard buttons dynamically, configure these tool permissions:

**Add to Claude Code Settings (for agent session):**

These commands should be pre-approved for the agent to run without prompts:

```
Bash(curl -s http://localhost:8081/inputs)
Bash(curl -s -X POST http://localhost:8081/update)
Bash(curl -s -X POST http://localhost:8081/state/update)
Bash(node agent-bridge.js)
Bash(~/.claude/tkeyboard-agent-start.sh)
Bash(cd bridge-server && node generate-images.js*)
Read(//Users/*/Documents/Arduino/tkeyboard-claude/**)
Read(/Users/*/Documents/Arduino/tkeyboard-claude/**)
Read(//tmp/tkeyboard-*.log)
Read(/tmp/tkeyboard-*.log)
```

**Why these permissions are needed:**
- **curl http://localhost:8081/inputs** - Poll keyboard for button presses (keeps bridge healthy)
- **curl POST /update** - Update button display based on context
- **curl POST /state/update** - Update keyboard state (thinking/idle/error)
- **node agent-bridge.js** - Start bridge server if not running
- **tkeyboard-agent-start.sh** - Launch input daemon for TTY injection
- **generate-images.js** - Create custom icons on-the-fly
- **Read permissions** - Access logs, configs, and generated images

**How to configure:**

1. Open Claude Code settings
2. Navigate to Tool Permissions or Allowed Tools
3. Add the patterns above to pre-approved tools list
4. Agent can now manage keyboard without manual approvals

**Security:**
- All commands are localhost-only (no external network access)
- File reads limited to project directory and temp logs
- Bridge server only accepts connections from localhost
- No sudo or elevated privileges required

## Security Notes

**AppleScript Accessibility Permissions:**
- Grants Terminal.app ability to simulate keypresses system-wide
- Only sends keystrokes when button is pressed on T-Keyboard
- No root/sudo privileges required
- Can be revoked anytime in System Settings → Privacy & Security → Accessibility

**Best Practices:**
- Only grant Accessibility permissions to trusted applications
- Review enabled apps periodically in Privacy & Security settings
- The input daemon only injects text from authenticated T-Keyboard device
- Button text is controlled by bridge server (localhost only)

## Uninstalling

```bash
# Remove daemon scripts
rm ~/.claude/tkeyboard-*

# Remove session files
rm ~/.claude/active-session.json
rm /tmp/tkeyboard-*.log

# Optionally remove thinking state hooks
rm ~/.claude/hooks/tkeyboard-thinking-*.sh

# Revoke Accessibility permissions
# Go to System Settings → Privacy & Security → Accessibility
# Remove Terminal.app from the list
```
