import fs from 'node:fs';
import path from 'node:path';
import type { Plugin, ResolvedConfig } from 'vite';

/** An icon or screenshot, given per colour scheme. */
export interface ManifestImageOptions {
  /** File in the build output, e.g. `favicon.svg`. */
  file: string;
  colorScheme: 'light' | 'dark';
}

export interface ManifestEntryOptions {
  title: string;
  /** Version of this entry. Defaults to `version` from package.json. */
  version?: string;
  /** Defaults to `description` from package.json. */
  description?: string;
  /** Page the entry opens; defaults to the base URL. */
  main?: string;
  icons?: ManifestImageOptions[];
  screenshots?: ManifestImageOptions[];
}

export interface ManifestOptions {
  /** Application name. Defaults to `name` from the project's package.json. */
  name?: string;
  entries: ManifestEntryOptions[];
  /** Manifest filename inside the build output. */
  fileName?: string;
}

interface ManifestImage {
  url: string;
  colorScheme: 'light' | 'dark';
}

interface ManifestFile {
  url: string;
  file: string;
  type?: string;
}

/**
 * Content type per extension, written into every file entry that has one.
 * The server falls back to `application/octet-stream` for a file listed
 * without a `type`, and a browser will not render an image or run a font
 * served as that — so anything shipped in `dist/` needs an entry here.
 */
const CONTENT_TYPES: Record<string, string> = {
  '.html': 'text/html',
  '.css': 'text/css',
  '.js': 'text/javascript',
  '.json': 'application/json',
  '.map': 'application/json',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.webp': 'image/webp',
  '.ico': 'image/x-icon',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
};

/**
 * `name`, `version` and `description` from the project's package.json. The
 * manifest name and every entry's version and description default to these,
 * so renaming, releasing or redescribing the package carries through to the
 * manifest without touching the Vite config.
 */
function readPackage(root: string): { name: string; version?: string; description?: string } {
  const file = path.join(root, 'package.json');
  const pkg = JSON.parse(fs.readFileSync(file, 'utf8')) as { name?: string; version?: string; description?: string };
  if (!pkg.name) {
    throw new Error(`${file} has no "name" to use as the manifest name`);
  }
  return { name: pkg.name, version: pkg.version, description: pkg.description };
}

function listFiles(dir: string, prefix = ''): string[] {
  return fs
    .readdirSync(dir, { withFileTypes: true })
    .flatMap((item) => {
      const relative = prefix ? `${prefix}/${item.name}` : item.name;
      return item.isDirectory()
        ? listFiles(path.join(dir, item.name), relative)
        : [relative];
    })
    .sort();
}

/**
 * Writes a `manifest.json` describing the build: the base URL it was built
 * for, its entry pages and every file in the output with the URL it is served
 * from. `index.html` is served at the base URL itself, so that is the URL it
 * gets — matching how the server resolves client routes.
 */
export default function manifestPlugin(options: ManifestOptions): Plugin {
  let config: ResolvedConfig;

  return {
    name: 'app-manifest',
    apply: 'build',

    configResolved(resolved) {
      config = resolved;
    },

    // closeBundle runs after the public directory has been copied, so the
    // listing covers every file that is actually shipped.
    closeBundle() {
      const outDir = path.resolve(config.root, config.build.outDir);
      if (!fs.existsSync(outDir)) return;

      // config.base always ends with a slash: '/' at the root, '/apps/' below it.
      const base = config.base === '/' ? '' : config.base.replace(/\/+$/, '');
      const urlFor = (file: string) => (file === 'index.html' ? base || '/' : `${base}/${file}`);

      const fileName = options.fileName ?? 'manifest.json';
      const files: ManifestFile[] = listFiles(outDir)
        .filter((file) => file !== fileName)
        .map((file) => {
          const url = urlFor(file);
          const type = CONTENT_TYPES[path.extname(file)];
          return type ? { url, file, type } : { url, file };
        });

      const toImages = (images: ManifestImageOptions[] = []): ManifestImage[] =>
        images.map((image) => ({ url: urlFor(image.file), colorScheme: image.colorScheme }));

      const pkg = readPackage(config.root);

      const manifest = {
        name: options.name ?? pkg.name,
        base: base || '/',
        entries: options.entries.map((entry) => {
          const icons = toImages(entry.icons);
          const screenshots = toImages(entry.screenshots);
          // Entries without a version or description anywhere are written
          // without the field, so a consumer can tell "none" from an empty one.
          const version = entry.version ?? pkg.version;
          const description = entry.description ?? pkg.description;
          return {
            title: entry.title,
            ...(version ? { version } : {}),
            ...(description ? { description } : {}),
            main: entry.main ?? (base || '/'),
            ...(icons.length ? { icons } : {}),
            ...(screenshots.length ? { screenshots } : {}),
          };
        }),
        files,
      };

      fs.writeFileSync(path.join(outDir, fileName), `${JSON.stringify(manifest, null, 2)}\n`);
      config.logger.info(`  ${fileName} written with ${files.length} files`);
    },
  };
}
