# TODO: Progressive Frame-by-Frame GIF Rendering

## Current Status (2025-10-26)

✅ **DONE:**
- GIF animation works (thinking.gif plays and loops)
- Watchdog timeout fixed (added `feedWatchdog()` calls during GIF playback)
- GIF loops continuously without device reboots

❌ **PROBLEM:**
- GIF playback blocks ESP32 processing
- Even with `loops=1`, a complete GIF cycle can take 2+ seconds (e.g., 30 frames × 400ms/frame = 12 seconds)
- During GIF playback, button presses are not detected
- Main loop is blocked until entire GIF cycle completes

## Proposed Solution: Frame-by-Frame Rendering

Instead of playing full GIF cycles in one blocking call, render ONE frame per main loop() iteration:

### Architecture:
```cpp
// Global state (already added to firmware)
struct {
    int8_t activeDisplay = -1;  // Which display shows active GIF? (-1 = none)
    String path;                 // Path to current GIF file
    bool initialized = false;    // Has GIF been opened?
    unsigned long lastFrameTime = 0;
    uint16_t frameDelay = 50;    // ms between frames
} gifState;
```

### Implementation Plan:

1. **initializeGIF(path, displayIndex)** - Opens GIF, starts animation
   - Opens GIF file with gif.open()
   - Sets gifState.activeDisplay = displayIndex
   - Sets gifState.initialized = true
   - Does NOT play frames yet

2. **advanceGIFFrame()** - Called every loop(), renders ONE frame
   - Check if (millis() - gifState.lastFrameTime >= gifState.frameDelay)
   - If yes: call gif.playFrame(true, NULL) ONCE
   - If frame returns false (end of GIF): call gif.reset() to loop
   - Update gifState.lastFrameTime = millis()
   - Return immediately to main loop

3. **stopGIF()** - Stops active GIF
   - gif.close()
   - gifState.activeDisplay = -1
   - gifState.initialized = false

4. **Periodic update in loop():**
   ```cpp
   // In main loop(), during IDLE/THINKING/WAITING_INPUT states:
   if (gifState.initialized && gifState.activeDisplay >= 0) {
       advanceGIFFrame();  // Render one frame, returns immediately
   }
   ```

### Benefits:
- Each loop() iteration: ~50-100ms (one frame) instead of 2-12 seconds (full cycle)
- Button presses detected every loop iteration
- Smooth animation maintained
- No watchdog timeouts
- Non-blocking WiFi/WebSocket handling

### References:
- Current GIF code: `arduino/TKeyboardClaude/TKeyboardClaude.ino:1220-1340`
- GIF state struct: Line 74-80
- AnimatedGIF library: https://github.com/bitbank2/AnimatedGIF

### Testing Plan:
1. Test with thinking.gif (3 frames, 400ms/frame)
2. Verify button presses work during GIF playback
3. Test with longer GIFs (10+ frames)
4. Verify no watchdog timeouts
5. Test state changes (new buttons arriving during GIF playback)
