import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import manifest from './tools/manifest-plugin';
import { resolveBaseUrl, toViteBase } from './tools/base-url.js';

/**
 * The API is served by the breakpad module inside `breakpad_server`, so
 * `npm run dev` proxies it there. Point it elsewhere with BREAKPAD_SERVER.
 */
const API_SERVER = process.env.BREAKPAD_SERVER ?? 'http://localhost:8033';

/** The API routes, right under the base URL; kRoutes in module.c. */
const API_PATHS = ['minidump_stackwalk', 'dump_syms', 'microdump_stackwalk'];

export default defineConfig(() => {
  const base = toViteBase(resolveBaseUrl());

  return {
    // `npm run build --base-url=/breakpad` builds the page for /breakpad/.
    base,
    plugins: [
      react(),
      // Writes dist/manifest.json describing the build: its base URL, entry
      // pages and every shipped file with the URL it is served from.
      manifest({
        // `name`, and each entry's `version` and `description`, are omitted on
        // purpose: they come from package.json.
        entries: [
          {
            title: 'Breakpad',
            icons: [
              { file: 'favicon.svg', colorScheme: 'light' },
              { file: 'favicon.svg', colorScheme: 'dark' },
            ],
            // The card shown for this app on the main page.
            screenshots: [
              { file: 'logo_wla.svg', colorScheme: 'light' },
              { file: 'logo_wla.svg', colorScheme: 'dark' },
            ],
          },
        ],
      }),
    ],
    server: {
      // build/ holds the bitmake tree (Breakpad, libnetq, OpenSSL…): watching it
      // runs out of inotify watches (ENOSPC), and the page never imports from it.
      watch: {
        ignored: ['**/build/**'],
      },
      proxy: {
        ...Object.fromEntries(API_PATHS.map((path) => [`${base}${path}`, { target: API_SERVER, changeOrigin: true }])),
      },
    },
    build: {
      // Emit the hashed JS/CSS next to index.html instead of into dist/assets/.
      assetsDir: '',
    },
  };
});
