import { useCallback, useEffect, useMemo, useState } from 'react';
import { homepage } from '../../package.json';
import { errorMessage, formatBytes, formatDate } from '../format';
import { MINIDUMPS_CHANGED, addMinidump, deleteMinidump, listMinidumps } from '../minidumpStore';
import { hrefFor, navigate } from '../router';
import type { Minidump } from '../types';
import DropZone from './DropZone';
import MinidumpDetail from './MinidumpDetail';

/** The last minidump opened in this browser; a per-viewer convenience, so storage errors are ignored. */
const LAST_OPENED_KEY = 'breakpad:last-minidump';

function readLastOpened(): string | null {
  try {
    return localStorage.getItem(LAST_OPENED_KEY);
  } catch {
    return null;
  }
}

function writeLastOpened(id: string): void {
  try {
    localStorage.setItem(LAST_OPENED_KEY, id);
  } catch {
    // Not remembered; the tab then opens with nothing selected.
  }
}

export default function MinidumpsPage({ selectedId }: { selectedId?: string }) {
  const [minidumps, setMinidumps] = useState<Minidump[] | null>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [filter, setFilter] = useState('');
  const [addError, setAddError] = useState<string | null>(null);

  const load = useCallback(async () => {
    try {
      setMinidumps(await listMinidumps());
      setLoadError(null);
    } catch (error) {
      setLoadError(`Browser storage is not available: ${errorMessage(error)}`);
    }
  }, []);

  useEffect(() => {
    load();
    window.addEventListener(MINIDUMPS_CHANGED, load);
    return () => window.removeEventListener(MINIDUMPS_CHANGED, load);
  }, [load]);

  const open = async (files: File[]) => {
    setAddError(null);
    let last: Minidump | null = null;
    for (const file of files) {
      try {
        last = await addMinidump(file, { name: file.name, source: 'file' });
      } catch (error) {
        setAddError(`${file.name}: ${errorMessage(error)}`);
      }
    }
    if (last) navigate({ page: 'minidumps', id: last.id });
  };

  /** Deletes the dump with its symbols and cached stack walk; leaves it if it was open. */
  const remove = async (item: Minidump) => {
    if (!window.confirm(`Delete ${item.name} from this browser? This cannot be undone.`)) return;
    try {
      await deleteMinidump(item.id);
      if (item.id === selectedId) navigate({ page: 'minidumps' }, { replace: true });
    } catch (error) {
      setLoadError(errorMessage(error));
    }
  };

  const visible = useMemo(() => {
    const needle = filter.trim().toLowerCase();
    if (!minidumps || !needle) return minidumps;
    return minidumps.filter((item) =>
      [item.name, item.product, item.version, item.source].some((value) => value?.toLowerCase().includes(needle)),
    );
  }, [minidumps, filter]);

  const selected = minidumps?.find((item) => item.id === selectedId);

  // Remember the open dump, so coming back to the Minidumps tab opens it again.
  useEffect(() => {
    if (selected) writeLastOpened(selected.id);
  }, [selected]);

  useEffect(() => {
    if (selectedId || !minidumps) return;
    const last = readLastOpened();
    if (last && minidumps.some((item) => item.id === last)) navigate({ page: 'minidumps', id: last }, { replace: true });
  }, [selectedId, minidumps]);

  return (
    <div className="split">
      <aside className="sidebar">
        <DropZone
          label="Open minidumps"
          hint="Kept in this browser only. The server gets a copy just while a tool runs on it."
          accept=".dmp,application/octet-stream"
          multiple
          onFiles={open}
        />
        {addError ? <p className="error">{addError}</p> : null}

        <div className="list-header">
          {/* No autocomplete: the browser's list of past entries would drop down over the first item. */}
          <input type="search" placeholder="Filter" autoComplete="off" value={filter} onChange={(event) => setFilter(event.target.value)} />
        </div>

        {loadError ? <p className="error">{loadError}</p> : null}
        {visible === null && !loadError ? <p className="muted">Loading…</p> : null}
        {visible?.length === 0 ? (
          <p className="muted empty">{minidumps?.length ? 'Nothing matches the filter.' : 'No minidumps in this browser yet. Open a .dmp file to start.'}</p>
        ) : null}
        <ul className="item-list">
          {visible?.map((item) => (
            <li key={item.id} className="item-with-remove">
              <a href={hrefFor({ page: 'minidumps', id: item.id })} className={item.id === selectedId ? 'selected' : ''}>
                <span className="item-title">{item.name}</span>
                <span className="item-meta">
                  {item.product ? `${item.product} ${item.version ?? ''} · ` : ''}
                  {formatBytes(item.size)} · {formatDate(item.created)}
                </span>
                {item.source !== 'file' ? <span className="badge">{item.source}</span> : null}
              </a>
              <button
                type="button"
                className="item-remove"
                title={`Delete ${item.name}`}
                aria-label={`Delete ${item.name}`}
                onClick={() => remove(item)}
              >
                <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" strokeWidth="2.2" strokeLinecap="round" aria-hidden>
                  <path d="M6 6l12 12M18 6L6 18" />
                </svg>
              </button>
            </li>
          ))}
        </ul>

        <footer className="sidebar-footer">
          Copyright © 2026 · MIT ·{' '}
          <a href={homepage} target="_blank" rel="noreferrer">
            GitHub
          </a>
        </footer>
      </aside>

      <section className="main">
        {selected ? (
          // Keyed by dump so switching dumps starts from fresh state.
          <MinidumpDetail key={selected.id} minidump={selected} />
        ) : selectedId && minidumps ? (
          <p className="error">This minidump is not in this browser.</p>
        ) : (
          <div className="placeholder">
            <h2>Minidumps</h2>
            <p>
              Open a minidump to see its stack trace. Minidumps stay in this browser: each tool run sends the dump to the
              server, which deletes its copy as soon as the run ends. Each minidump has its own list of symbol files, sent
              along with it on every stack walk.
            </p>
          </div>
        )}
      </section>
    </div>
  );
}
