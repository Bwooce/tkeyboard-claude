#!/usr/bin/env node

/**
 * Create animated GIF for T-Keyboard-S3
 * Generates a simple "thinking" animation with animated dots
 */

const fs = require('fs');
const path = require('path');
const { createCanvas } = require('@napi-rs/canvas');
const GIFEncoder = require('gifencoder');

// Configuration
const IMAGE_SIZE = 128;
const FRAME_COUNT = 3;
const FRAME_DELAY = 400; // milliseconds
const OUTPUT_DIR = path.join(__dirname, '../images/cache');

// Ensure output directory exists
if (!fs.existsSync(OUTPUT_DIR)) {
    fs.mkdirSync(OUTPUT_DIR, { recursive: true });
}

// Create GIF encoder
const encoder = new GIFEncoder(IMAGE_SIZE, IMAGE_SIZE);
const outputPath = path.join(OUTPUT_DIR, 'thinking.gif');
const stream = fs.createWriteStream(outputPath);

encoder.createReadStream().pipe(stream);
encoder.start();
encoder.setRepeat(0);   // 0 = loop forever
encoder.setDelay(FRAME_DELAY);
encoder.setQuality(10); // 1-20, lower is better

console.log('Generating animated GIF: thinking.gif');
console.log(`Frames: ${FRAME_COUNT}, Delay: ${FRAME_DELAY}ms, Size: ${IMAGE_SIZE}x${IMAGE_SIZE}`);

// Generate frames
for (let frame = 0; frame < FRAME_COUNT; frame++) {
    const canvas = createCanvas(IMAGE_SIZE, IMAGE_SIZE);
    const ctx = canvas.getContext('2d');

    // Black background
    ctx.fillStyle = '#000000';
    ctx.fillRect(0, 0, IMAGE_SIZE, IMAGE_SIZE);

    // Draw "thinking" text
    ctx.fillStyle = '#FFD700'; // Gold color
    ctx.font = 'bold 24px Arial';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('Thinking', IMAGE_SIZE / 2, IMAGE_SIZE / 2 - 20);

    // Draw animated dots
    const center = IMAGE_SIZE / 2;
    const dotY = center + 15;
    const dotSpacing = 15;
    const dotRadius = 4;

    for (let i = 0; i < 3; i++) {
        const dotX = center - dotSpacing + (i * dotSpacing);

        // Only draw dots up to current frame
        if (i <= frame) {
            ctx.fillStyle = '#FFD700';
            ctx.beginPath();
            ctx.arc(dotX, dotY, dotRadius, 0, Math.PI * 2);
            ctx.fill();
        }
    }

    // Add frame to GIF
    encoder.addFrame(ctx);
    console.log(`✓ Frame ${frame + 1}/${FRAME_COUNT} generated`);
}

encoder.finish();

stream.on('finish', () => {
    const stats = fs.statSync(outputPath);
    console.log(`\n✓ GIF created: ${outputPath}`);
    console.log(`  Size: ${stats.size} bytes (${(stats.size / 1024).toFixed(2)} KB)`);
});

stream.on('error', (err) => {
    console.error('Error creating GIF:', err);
    process.exit(1);
});
