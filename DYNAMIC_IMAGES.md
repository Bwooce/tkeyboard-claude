# Dynamic Image Generation - Implementation Guide

## Current Status

**Already Implemented:**
- ✅ Context detection in agent
- ✅ RGB565 image conversion (`generate-images.js`)
- ✅ WebSocket image transfer protocol
- ✅ ESP32 SPIFFS caching (13MB available)
- ✅ Image loading from cache
- ✅ Base64 image transmission

**Not Yet Implemented:**
- ❌ Agent calling image generation
- ❌ On-demand image creation

## Implementation (30-60 minutes)

### Option 1: Agent Generates Images Directly

The agent can use the existing `generate-images.js` functions:

**Enhanced Agent Prompt:**
```
When updating keyboard display:

1. Analyze context and determine button options
2. For each button, check if image exists
3. If image doesn't exist, generate it:
   - Run: node bridge-server/generate-images.js --create <icon-type>
   - Or use Bash to call image generation
4. Send image to keyboard via POST to bridge server
5. Keyboard caches automatically in SPIFFS

For example, if debugging context detected:
- Generate "debug.rgb" with bug icon
- Generate "trace.rgb" with stack icon
- Send to keyboard
```

### Option 2: Extend generate-images.js

Add dynamic generation function:

```javascript
// In generate-images.js
function generateDynamicIcon(type, text, color) {
    const canvas = createCanvas(128, 128);
    const ctx = canvas.getContext('2d');

    // Clear background
    ctx.fillStyle = '#000000';
    ctx.fillRect(0, 0, 128, 128);

    // Draw based on type
    switch(type) {
        case 'text':
            ctx.fillStyle = color || '#FFFFFF';
            ctx.font = 'bold 24px Arial';
            ctx.textAlign = 'center';
            ctx.fillText(text, 64, 64);
            break;

        case 'emoji':
            ctx.font = '64px Arial';
            ctx.textAlign = 'center';
            ctx.fillText(text, 64, 74);
            break;

        case 'symbol':
            drawSymbol(ctx, text, color);
            break;
    }

    // Convert and save
    const rgb565 = canvasToRgb565(canvas);
    const filename = `${sanitize(text)}.rgb`;
    fs.writeFileSync(`images/cache/${filename}`, rgb565);

    return filename;
}
```

### Option 3: Use Web API for Image Generation

Agent can fetch images from online services:

```javascript
// Agent code
async function getIcon(name) {
    // Use a free icon API
    const response = await fetch(
        `https://api.iconify.design/${name}.svg?width=128&height=128`
    );
    const svg = await response.text();

    // Convert SVG to PNG to RGB565
    const png = await convertSvgToPng(svg);
    const rgb565 = await convertToRgb565(png);

    // Send to keyboard
    sendImage(rgb565, `${name}.rgb`);
}
```

## Complete Example

**Agent that generates images on-the-fly:**

```
I need you to monitor my T-Keyboard and generate custom images:

1. Start bridge server as before

2. Monitor for inputs every 2 seconds

3. When updating display options:
   a. Determine context (error, code review, etc.)
   b. Choose appropriate icons
   c. For each icon:
      - Check if it exists in images/cache/
      - If not, generate it:
        * Use Bash to create simple text-based icons
        * Or use generate-images.js functions
      - Send image data to keyboard

4. Example contexts:
   - Error: Generate "🐛 Debug", "🔄 Retry", "⏭ Skip"
   - Code: Generate "▶️ Run", "🧪 Test", "♻️ Refactor"
   - Planning: Generate "📋 List", "🎯 Focus", "🔀 Branch"

5. Image generation method:
   - Simple: Render emoji at 64px size
   - Advanced: Draw shapes and text
   - Cache all generated images

Begin monitoring with image generation.
```

## WebSocket Protocol for Images

Already implemented in ESP32:

```json
{
  "type": "image",
  "name": "debug.rgb",
  "data": "base64_encoded_rgb565_data"
}
```

ESP32 receives, decodes, saves to SPIFFS.

## Benefits of Dynamic Generation

1. **Context-Specific Icons**: Perfect match for each situation
2. **No Pre-Generation**: Only create what's needed
3. **Unlimited Variety**: Not limited to pre-made icons
4. **Text-Based Options**: Can show variable text on buttons
5. **Emoji Support**: Modern, recognizable symbols

## Performance

- **Generation Time**: ~100ms per icon (Node.js Canvas)
- **Transfer Time**: ~200ms (RGB565 is 32KB)
- **Cache Hit**: Instant (loads from SPIFFS)
- **First Use**: ~300ms total
- **Subsequent**: 0ms (cached)

## Example Contexts

```javascript
const contexts = {
    debugging: {
        icons: ['🐛', '📍', '▶️', '⏭'],
        fallback: ['Debug', 'Break', 'Step', 'Skip']
    },
    testing: {
        icons: ['✅', '❌', '🔄', '📊'],
        fallback: ['Pass', 'Fail', 'Retry', 'Report']
    },
    git: {
        icons: ['💾', '🔀', '📤', '↩️'],
        fallback: ['Commit', 'Branch', 'Push', 'Revert']
    },
    file_ops: {
        icons: ['📁', '📝', '🗑️', '📋'],
        fallback: ['Open', 'Edit', 'Delete', 'Copy']
    }
}
```

## Conclusion

Adding dynamic image generation is **trivial** because:

1. Image generation code already exists
2. Transfer protocol already works
3. ESP32 caching already functional
4. Agent already detects context

Just need to connect these pieces with a few lines of code or an enhanced agent prompt!