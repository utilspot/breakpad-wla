import { useState } from 'react';
import DumpSymsPage from './components/DumpSymsPage';
import MicrodumpPage from './components/MicrodumpPage';
import MinidumpsPage from './components/MinidumpsPage';
import { BASE_URL } from './base';
import { hrefFor, useRoute, type Route } from './router';

const PAGES: { page: Route['page']; label: string }[] = [
  { page: 'minidumps', label: 'Minidumps' },
  { page: 'dumpsyms', label: 'DumpSyms' },
  { page: 'microdump', label: 'Microdump' },
];

export default function App() {
  const route = useRoute();

  // DumpSyms stays mounted once opened, only hidden while another page shows, so
  // coming back finds its files, options, result and any running upload as left.
  const [dumpSymsOpened, setDumpSymsOpened] = useState(route.page === 'dumpsyms');
  if (route.page === 'dumpsyms' && !dumpSymsOpened) setDumpSymsOpened(true);

  return (
    <div className="app">
      <header className="app-header">
        <div className="header-start">
          {/* Same URL as the manifest entry's `main` (tools/manifest-plugin.ts). */}
          <a className="brand" href={BASE_URL || '/'}>
            <img src={`${import.meta.env.BASE_URL}favicon.svg`} alt="" width={28} height={28} />
            <span>Breakpad</span>
          </a>
          {/* Served below the site root: a way back up to the site's home page. */}
          {BASE_URL ? (
            <a className="header-link" href="/">
              Home
            </a>
          ) : null}
          <nav className="app-nav">
            {PAGES.map(({ page, label }) => (
              <a
                key={page}
                href={hrefFor({ page } as Route)}
                className={route.page === page ? 'header-link active' : 'header-link'}
                aria-current={route.page === page ? 'page' : undefined}
              >
                {label}
              </a>
            ))}
          </nav>
        </div>
      </header>
      <main className="app-main">
        {route.page === 'minidumps' ? <MinidumpsPage selectedId={route.id} /> : null}
        {dumpSymsOpened ? (
          <div hidden={route.page !== 'dumpsyms'}>
            <DumpSymsPage />
          </div>
        ) : null}
        {route.page === 'microdump' ? <MicrodumpPage /> : null}
      </main>
    </div>
  );
}
