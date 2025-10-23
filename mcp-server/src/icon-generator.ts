import { execSync } from 'child_process';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

/**
 * Ensures an icon exists in the cache, generating it if needed
 * @param emoji Emoji character to render
 * @param name Base name for the file (without .rgb extension)
 * @returns Filename (e.g., "commit.rgb")
 */
export async function ensureIcon(emoji: string, name: string): Promise<string> {
  const filename = `${name}.rgb`;
  const cachePath = path.join(__dirname, '../../images/cache', filename);

  if (fs.existsSync(cachePath)) {
    console.log(`[Icon] Cache hit: ${filename}`);
    return filename;
  }

  // Generate using existing script
  console.log(`[Icon] Generating ${filename} with emoji ${emoji}`);

  try {
    execSync(
      `node generate-images.js --emoji "${emoji}" ${name}`,
      {
        cwd: path.join(__dirname, '../../bridge-server'),
        stdio: 'pipe'
      }
    );

    console.log(`[Icon] Created: ${filename}`);
    return filename;
  } catch (error) {
    console.error(`[Icon] Generation failed for ${filename}:`, error);
    throw new Error(`Failed to generate icon: ${filename}`);
  }
}

/**
 * Ensures multiple icons exist, generating them in parallel
 */
export async function ensureIcons(
  specs: Array<{ emoji: string; name: string }>
): Promise<string[]> {
  return Promise.all(
    specs.map(spec => ensureIcon(spec.emoji, spec.name))
  );
}
