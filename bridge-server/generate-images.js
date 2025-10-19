#!/usr/bin/env node

/**
 * Image generator and converter for T-Keyboard-S3 displays
 * Creates 128x128 RGB565 images for common actions
 */

const fs = require('fs');
const path = require('path');
const { createCanvas } = require('@napi-rs/canvas');

// Configuration
const IMAGE_SIZE = 128;
const OUTPUT_DIR = path.join(__dirname, '../images/generated');
const CACHE_DIR = path.join(__dirname, '../images/cache');

// Ensure output directories exist
[OUTPUT_DIR, CACHE_DIR].forEach(dir => {
    if (!fs.existsSync(dir)) {
        fs.mkdirSync(dir, { recursive: true });
    }
});

/**
 * Convert RGB to RGB565 format
 */
function rgbToRgb565(r, g, b) {
    const r5 = Math.round(r * 31 / 255);
    const g6 = Math.round(g * 63 / 255);
    const b5 = Math.round(b * 31 / 255);
    return (r5 << 11) | (g6 << 5) | b5;
}

/**
 * Convert canvas to RGB565 buffer
 */
function canvasToRgb565(canvas) {
    const ctx = canvas.getContext('2d');
    const imageData = ctx.getImageData(0, 0, IMAGE_SIZE, IMAGE_SIZE);
    const buffer = Buffer.alloc(IMAGE_SIZE * IMAGE_SIZE * 2);

    let offset = 0;
    for (let i = 0; i < imageData.data.length; i += 4) {
        const r = imageData.data[i];
        const g = imageData.data[i + 1];
        const b = imageData.data[i + 2];
        const rgb565 = rgbToRgb565(r, g, b);

        // Write as big-endian
        buffer.writeUInt16BE(rgb565, offset);
        offset += 2;
    }

    return buffer;
}

/**
 * Generate a simple icon
 */
function generateIcon(ctx, type, color = '#FFFFFF') {
    // Clear background
    ctx.fillStyle = '#000000';
    ctx.fillRect(0, 0, IMAGE_SIZE, IMAGE_SIZE);

    // Set drawing style
    ctx.strokeStyle = color;
    ctx.fillStyle = color;
    ctx.lineWidth = 3;

    const center = IMAGE_SIZE / 2;
    const size = IMAGE_SIZE * 0.6;

    switch(type) {
        case 'yes':
            // Checkmark
            ctx.strokeStyle = '#00FF00';
            ctx.lineWidth = 8;
            ctx.beginPath();
            ctx.moveTo(center - size/3, center);
            ctx.lineTo(center - size/6, center + size/3);
            ctx.lineTo(center + size/3, center - size/3);
            ctx.stroke();
            break;

        case 'no':
            // X mark
            ctx.strokeStyle = '#FF0000';
            ctx.lineWidth = 8;
            ctx.beginPath();
            ctx.moveTo(center - size/3, center - size/3);
            ctx.lineTo(center + size/3, center + size/3);
            ctx.moveTo(center + size/3, center - size/3);
            ctx.lineTo(center - size/3, center + size/3);
            ctx.stroke();
            break;

        case 'stop':
            // Stop sign (octagon)
            ctx.fillStyle = '#FF0000';
            ctx.beginPath();
            const r = size/2;
            for (let i = 0; i < 8; i++) {
                const angle = (i * Math.PI / 4) - Math.PI/8;
                const x = center + r * Math.cos(angle);
                const y = center + r * Math.sin(angle);
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.closePath();
            ctx.fill();

            // STOP text
            ctx.fillStyle = '#FFFFFF';
            ctx.font = 'bold 20px Arial';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillText('STOP', center, center);
            break;

        case 'continue':
            // Arrow right
            ctx.fillStyle = '#0080FF';
            ctx.beginPath();
            ctx.moveTo(center - size/3, center - size/4);
            ctx.lineTo(center, center - size/4);
            ctx.lineTo(center, center - size/2);
            ctx.lineTo(center + size/3, center);
            ctx.lineTo(center, center + size/2);
            ctx.lineTo(center, center + size/4);
            ctx.lineTo(center - size/3, center + size/4);
            ctx.closePath();
            ctx.fill();
            break;

        case 'more':
            // Three dots
            ctx.fillStyle = '#808080';
            const dotSize = 12;
            const spacing = 30;
            for (let i = -1; i <= 1; i++) {
                ctx.beginPath();
                ctx.arc(center + i * spacing, center, dotSize, 0, Math.PI * 2);
                ctx.fill();
            }
            break;

        case 'help':
            // Question mark
            ctx.fillStyle = '#FFD700';
            ctx.font = 'bold 60px Arial';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillText('?', center, center);
            break;

        case 'thinking':
            // Animated dots (single frame)
            ctx.fillStyle = '#FFD700';
            ctx.beginPath();
            ctx.arc(center - 30, center, 8, 0, Math.PI * 2);
            ctx.fill();
            ctx.beginPath();
            ctx.arc(center, center, 8, 0, Math.PI * 2);
            ctx.fill();
            ctx.beginPath();
            ctx.arc(center + 30, center, 8, 0, Math.PI * 2);
            ctx.fill();
            break;

        case 'approve':
            // Thumbs up
            ctx.fillStyle = '#00FF00';
            // Simple representation
            ctx.fillRect(center - 15, center - 10, 30, 40);
            ctx.fillRect(center - 25, center - 20, 20, 20);
            break;

        case 'reject':
            // Thumbs down
            ctx.fillStyle = '#FF0000';
            // Simple representation
            ctx.fillRect(center - 15, center - 30, 30, 40);
            ctx.fillRect(center - 25, center + 10, 20, 20);
            break;

        case 'test':
            // Flask/beaker icon
            ctx.strokeStyle = '#00FFFF';
            ctx.lineWidth = 4;
            ctx.beginPath();
            // Flask shape
            ctx.moveTo(center - 20, center - 30);
            ctx.lineTo(center - 20, center);
            ctx.quadraticCurveTo(center - 20, center + 20, center - 30, center + 30);
            ctx.lineTo(center + 30, center + 30);
            ctx.quadraticCurveTo(center + 20, center + 20, center + 20, center);
            ctx.lineTo(center + 20, center - 30);
            ctx.stroke();

            // Liquid inside
            ctx.fillStyle = '#00FFFF';
            ctx.globalAlpha = 0.5;
            ctx.fillRect(center - 15, center, 30, 25);
            ctx.globalAlpha = 1.0;
            break;

        case 'refactor':
            // Gear icon
            ctx.fillStyle = '#808080';
            const teeth = 8;
            const outerRadius = size/2;
            const innerRadius = size/3;

            ctx.beginPath();
            for (let i = 0; i < teeth * 2; i++) {
                const angle = (i * Math.PI) / teeth;
                const radius = i % 2 === 0 ? outerRadius : innerRadius;
                const x = center + radius * Math.cos(angle);
                const y = center + radius * Math.sin(angle);
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.closePath();
            ctx.fill();

            // Center hole
            ctx.fillStyle = '#000000';
            ctx.beginPath();
            ctx.arc(center, center, size/6, 0, Math.PI * 2);
            ctx.fill();
            break;

        default:
            // Generic icon - circle
            ctx.strokeStyle = color;
            ctx.lineWidth = 4;
            ctx.beginPath();
            ctx.arc(center, center, size/2, 0, Math.PI * 2);
            ctx.stroke();
    }
}

/**
 * Generate all standard images
 */
function generateAllImages() {
    const images = [
        { name: 'yes', type: 'yes' },
        { name: 'no', type: 'no' },
        { name: 'stop', type: 'stop' },
        { name: 'continue', type: 'continue' },
        { name: 'more', type: 'more' },
        { name: 'help', type: 'help' },
        { name: 'thinking', type: 'thinking' },
        { name: 'approve', type: 'approve' },
        { name: 'reject', type: 'reject' },
        { name: 'test', type: 'test' },
        { name: 'refactor', type: 'refactor' }
    ];

    console.log('Generating images...\n');

    images.forEach(({ name, type }) => {
        const canvas = createCanvas(IMAGE_SIZE, IMAGE_SIZE);
        const ctx = canvas.getContext('2d');

        // Generate the icon
        generateIcon(ctx, type);

        // Convert to RGB565
        const rgb565Buffer = canvasToRgb565(canvas);

        // Save RGB565 file
        const rgbPath = path.join(CACHE_DIR, `${name}.rgb`);
        fs.writeFileSync(rgbPath, rgb565Buffer);
        console.log(`✓ Generated ${name}.rgb (${rgb565Buffer.length} bytes)`);

        // Also save PNG for reference
        const pngPath = path.join(OUTPUT_DIR, `${name}.png`);
        const pngBuffer = canvas.toBuffer('image/png');
        fs.writeFileSync(pngPath, pngBuffer);
        console.log(`✓ Generated ${name}.png (reference)`);
    });

    console.log('\nImage generation complete!');
    console.log(`RGB565 files saved to: ${CACHE_DIR}`);
    console.log(`PNG references saved to: ${OUTPUT_DIR}`);
}

/**
 * Convert a PNG image to RGB565
 */
async function convertPngToRgb565(inputPath, outputPath) {
    const sharp = require('sharp');

    try {
        // Read and resize image
        const { data, info } = await sharp(inputPath)
            .resize(IMAGE_SIZE, IMAGE_SIZE, { fit: 'cover' })
            .raw()
            .toBuffer({ resolveWithObject: true });

        // Convert to RGB565
        const rgb565Buffer = Buffer.alloc(IMAGE_SIZE * IMAGE_SIZE * 2);
        let offset = 0;

        for (let i = 0; i < data.length; i += info.channels) {
            const r = data[i];
            const g = data[i + 1];
            const b = data[i + 2];
            const rgb565 = rgbToRgb565(r, g, b);

            rgb565Buffer.writeUInt16BE(rgb565, offset);
            offset += 2;
        }

        fs.writeFileSync(outputPath, rgb565Buffer);
        console.log(`Converted ${inputPath} to ${outputPath}`);

    } catch (err) {
        console.error(`Failed to convert ${inputPath}:`, err.message);
    }
}

/**
 * Generate an emoji-based icon
 */
function generateEmojiIcon(emoji, name) {
    const canvas = createCanvas(IMAGE_SIZE, IMAGE_SIZE);
    const ctx = canvas.getContext('2d');

    // Black background
    ctx.fillStyle = '#000000';
    ctx.fillRect(0, 0, IMAGE_SIZE, IMAGE_SIZE);

    // Draw emoji
    ctx.font = '64px Arial, "Apple Color Emoji", "Segoe UI Emoji"';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(emoji, IMAGE_SIZE / 2, IMAGE_SIZE / 2);

    // Convert to RGB565
    const rgb565Buffer = canvasToRgb565(canvas);

    // Save
    const rgbPath = path.join(CACHE_DIR, `${name}.rgb`);
    fs.writeFileSync(rgbPath, rgb565Buffer);
    console.log(`✓ Generated ${name}.rgb from emoji: ${emoji}`);

    return rgbPath;
}

/**
 * Generate a text-based icon
 */
function generateTextIcon(text, color, name) {
    const canvas = createCanvas(IMAGE_SIZE, IMAGE_SIZE);
    const ctx = canvas.getContext('2d');

    // Black background
    ctx.fillStyle = '#000000';
    ctx.fillRect(0, 0, IMAGE_SIZE, IMAGE_SIZE);

    // Draw text
    ctx.fillStyle = color || '#FFFFFF';
    const fontSize = text.length > 4 ? 20 : 28;
    ctx.font = `bold ${fontSize}px Arial`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(text, IMAGE_SIZE / 2, IMAGE_SIZE / 2);

    // Convert to RGB565
    const rgb565Buffer = canvasToRgb565(canvas);

    // Save
    const rgbPath = path.join(CACHE_DIR, `${name}.rgb`);
    fs.writeFileSync(rgbPath, rgb565Buffer);
    console.log(`✓ Generated ${name}.rgb with text: "${text}"`);

    return rgbPath;
}

// Main execution
if (require.main === module) {
    const args = process.argv.slice(2);

    if (args.length >= 2 && args[0] === '--emoji') {
        // Generate emoji icon
        const emoji = args[1];
        const name = args[2] || emoji.codePointAt(0).toString(16);
        generateEmojiIcon(emoji, name);

    } else if (args.length >= 2 && args[0] === '--text') {
        // Generate text icon
        const text = args[1];
        const color = args[2] || '#FFFFFF';
        const name = args[3] || text.toLowerCase().replace(/\s+/g, '_');
        generateTextIcon(text, color, name);

    } else if (args.length === 2 && args[0] === '--convert') {
        // Convert a single PNG file
        const inputFile = args[1];
        const outputFile = path.join(CACHE_DIR,
            path.basename(inputFile, '.png') + '.rgb');
        convertPngToRgb565(inputFile, outputFile);

    } else {
        // Generate all standard images
        generateAllImages();
    }
}

module.exports = {
    generateIcon,
    canvasToRgb565,
    convertPngToRgb565,
    generateEmojiIcon,
    generateTextIcon
};