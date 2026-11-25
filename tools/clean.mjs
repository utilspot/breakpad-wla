// Deletes everything that is built, installed or written at run time.
// Usage: npm run clean

import { rmSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = dirname(dirname(fileURLToPath(import.meta.url)));

for (const name of ['build', 'dist', 'node_modules', 'breakpad-data']) {
  rmSync(join(root, name), { recursive: true, force: true });
}
