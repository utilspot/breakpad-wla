import { useEffect, useState } from 'react';

/**
 * Pages live in the URL hash (`#/minidumps/<id>`), so the server only ever
 * serves index.html at the base URL and a reload keeps the current page.
 */
export type Route =
  | { page: 'minidumps'; id?: string }
  | { page: 'dumpsyms' }
  | { page: 'microdump' };

export function parseHash(hash: string): Route {
  const [page, id] = hash.replace(/^#\/?/, '').split('/').map(decodeURIComponent);
  switch (page) {
    case 'dumpsyms':
    case 'microdump':
      return { page };
    default:
      return id ? { page: 'minidumps', id } : { page: 'minidumps' };
  }
}

export const hrefFor = (route: Route): string =>
  route.page === 'minidumps' && route.id ? `#/minidumps/${encodeURIComponent(route.id)}` : `#/${route.page}`;

/** `replace` swaps the current history entry instead of adding one. */
export function navigate(route: Route, { replace = false } = {}): void {
  if (replace) window.location.replace(hrefFor(route));
  else window.location.hash = hrefFor(route);
}

export function useRoute(): Route {
  const [route, setRoute] = useState(() => parseHash(window.location.hash));
  useEffect(() => {
    const onChange = () => setRoute(parseHash(window.location.hash));
    window.addEventListener('hashchange', onChange);
    return () => window.removeEventListener('hashchange', onChange);
  }, []);
  return route;
}
