# BUG: Claude Code TUI Input Handling

## Issue Description

When Claude Code's TUI (Terminal User Interface) prompts the user for input with multiple options, pressing ANY key on the T-Keyboard will accept the FIRST option in the list, regardless of which button was pressed or what text it sends.

## Example Scenario

**Claude Code Prompt:**
```
Choose an action:
  yes
  always
  no
```

**Expected Behavior:**
- Pressing button labeled "Yes" → sends "yes" → selects "yes"
- Pressing button labeled "No" → sends "no" → selects "no"
- Pressing button labeled "Always" → sends "always" → selects "always"

**Actual Behavior:**
- Pressing ANY button → accepts first option ("yes")
- The text sent by the button is ignored
- The TUI only checks IF a key was pressed, not WHAT was pressed

## Root Cause

The Claude Code TUI prompt handling appears to:
1. Display a list of options
2. Wait for ANY keypress event
3. Accept the first option immediately upon detecting a keypress
4. NOT actually read or parse the input text sent

This makes the T-Keyboard buttons non-functional for TUI prompts, as all buttons have the same effect (accepting the first option).

## Impact

- **Critical usability issue** for T-Keyboard integration with Claude Code TUI
- Users cannot make selections from TUI prompts using the keyboard
- Defeats the purpose of having multiple labeled buttons
- Forces users to use the main keyboard for TUI interactions

## Workaround

None available. Users must use the main keyboard for TUI prompts.

## Affected Components

- Claude Code TUI prompting system
- T-Keyboard input daemon (sends text via AppleScript, but text is ignored)
- Any workflow involving TUI yes/no/always/choice prompts

## Reproduction Steps

1. Start Claude Code with T-Keyboard system active
2. Trigger a TUI prompt (e.g., file overwrite confirmation)
3. Press any T-Keyboard button (button 1, 2, 3, or 4)
4. Observe that the first option is ALWAYS selected

## Potential Solutions

### Option 1: Navigation Keys (Arrow + Enter)
Modify input daemon to send arrow key sequences:
- Button 1 → "↑↑↑ + Enter" (select first option)
- Button 2 → "↑↑ + Enter" (select second option)
- Button 3 → "↑ + Enter" (select third option)
- Button 4 → "Enter" (select fourth option)

**Issues:**
- Requires knowing how many options are in the list
- Fragile - depends on TUI implementation details
- May not work if list wraps or has different navigation

### Option 2: Type Option Text
Send the full option text (e.g., "yes", "no", "always") followed by Enter.

**Issues:**
- Currently doesn't work - TUI appears to ignore typed text
- May require TUI changes

### Option 3: Fix Claude Code TUI
Request Anthropic to fix the TUI input handling to:
- Read and parse the actual input text
- Match input text against available options
- Accept the matching option (not just the first one)

**This is the ideal solution.**

## Related Files

- `installation/tkeyboard-input-daemon.sh` - Input injection via AppleScript
- `mcp-server/src/tkeyboard-server.ts` - Queues button text for injection

## Status

**Open** - No current fix. Documenting for future reference.

## Date Reported

2025-10-26
