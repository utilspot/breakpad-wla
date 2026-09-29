/**
 * Path prefix the app is served under, injected by Vite from its `base`
 * option — which comes from `--base-url=` (see tools/base-url.js).
 *
 * `''` at the site root, `'/apps'` when built or run with `--base-url=/apps`.
 */
export const BASE_URL = import.meta.env.BASE_URL.replace(/\/+$/, '');

/** Turns an app-relative path (`/api/sets`, `/apps/mail`) into a real URL. */
export const withBase = (path: string): string => `${BASE_URL}${path}`;
