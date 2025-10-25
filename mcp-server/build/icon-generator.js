import { execSync } from 'child_process';
import * as fs from 'fs';
import * as path from 'path';
import * as crypto from 'crypto';
import { fileURLToPath } from 'url';
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
/**
 * Generate hash from emoji content (used for cache key)
 */
function hashEmoji(emoji) {
    return crypto.createHash('md5').update(emoji).digest('hex').substring(0, 8);
}
/**
 * Ensures an icon exists in the cache, generating it if needed
 * @param emoji Emoji character to render
 * @param name Base name for the file (for logging/debugging only)
 * @returns Filename (e.g., "icon-a3f8b2d4.rgb")
 */
export async function ensureIcon(emoji, name) {
    const hash = hashEmoji(emoji);
    const filename = `icon-${hash}.rgb`;
    const cachePath = path.join(__dirname, '../../images/cache', filename);
    if (fs.existsSync(cachePath)) {
        console.log(`[Icon] Cache hit: ${filename} (${name}: ${emoji})`);
        return filename;
    }
    // Generate using existing script
    console.log(`[Icon] Generating ${filename} for "${name}" with emoji ${emoji}`);
    try {
        execSync(`node generate-images.js --emoji "${emoji}" icon-${hash}`, {
            cwd: path.join(__dirname, '../../icon-scripts'),
            stdio: 'pipe'
        });
        console.log(`[Icon] Created: ${filename}`);
        return filename;
    }
    catch (error) {
        console.error(`[Icon] Generation failed for ${filename}:`, error);
        throw new Error(`Failed to generate icon: ${filename}`);
    }
}
/**
 * Ensures multiple icons exist, generating them in parallel
 */
export async function ensureIcons(specs) {
    return Promise.all(specs.map(spec => ensureIcon(spec.emoji, spec.name)));
}
