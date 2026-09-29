/**
 * Base URL (path prefix) the whole app is served under.
 *
 * Set it with `--base-url=<path>` on any npm script, e.g.
 *
 *   npm run dev   --base-url=/breakpad
 *   npm run build --base-url=/breakpad
 *
 * npm exposes that flag as `npm_config_base_url`; `BASE_URL` in the
 * environment and a plain `--base-url=` argv flag work too, for use without
 * npm. Read by vite.config.ts; CMake passes BREAKPAD_BASE_URL through it.
 */

const FLAG = '--base-url=';

/** '' for the site root, otherwise '/prefix' with no trailing slash. */
export function normalizeBaseUrl(raw) {
  const trimmed = String(raw ?? '').trim();
  if (!trimmed || trimmed === '/') return '';
  const withLeadingSlash = trimmed.startsWith('/') ? trimmed : `/${trimmed}`;
  // Collapse repeated slashes so e.g. '//apps//' cannot become a
  // protocol-relative '//apps'.
  return withLeadingSlash.replace(/\/{2,}/g, '/').replace(/\/+$/, '');
}

export function resolveBaseUrl(argv = process.argv, env = process.env) {
  const flag = argv.find((arg) => arg.startsWith(FLAG));
  const raw = flag ? flag.slice(FLAG.length) : (env.npm_config_base_url ?? env.BASE_URL ?? '');
  return normalizeBaseUrl(raw);
}

/** Vite's `base`, which always has a trailing slash. */
export function toViteBase(baseUrl) {
  return baseUrl ? `${baseUrl}/` : '/';
}
