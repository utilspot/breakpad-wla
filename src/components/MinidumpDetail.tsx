import { useEffect, useRef, useState } from 'react';
import { ApiError, runStackwalk } from '../api';
import { addDumpSymbol, removeDumpSymbol, useDumpSymbols } from '../dumpSymbolStore';
import { getMinidumpBlob } from '../minidumpStore';
import { cacheStackwalk, getCachedStackwalk, type CachedStackwalk } from '../stackwalkCache';
import { errorMessage, formatBytes, formatDate, saveBlob } from '../format';
import type { Minidump, StackwalkOptions, ToolResult } from '../types';
import SymbolList, { useFileDrop, useSymbolAdder } from './SymbolList';
import StackwalkResult from './StackwalkResult';
import UploadProgressLine from './UploadProgressLine';

function Checkbox({ label, checked, onChange, hint }: { label: string; checked: boolean; onChange: (value: boolean) => void; hint?: string }) {
  return (
    <label className="check" title={hint}>
      <input type="checkbox" checked={checked} onChange={(event) => onChange(event.target.checked)} />
      {label}
    </label>
  );
}

/** One circular arrow, like a browser's reload button. */
function RefreshIcon() {
  return (
    <svg viewBox="0 0 24 24" width="18" height="18" fill="currentColor" aria-hidden>
      <path d="M17.65 6.35A7.96 7.96 0 0 0 12 4a8 8 0 1 0 7.73 10h-2.08A6 6 0 1 1 12 6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z" />
    </svg>
  );
}

/** What a stack walk uploads: the dump and its symbol files. */
function uploadWhat(files: number): string {
  return files > 1 ? `the minidump and ${files - 1} symbol file${files === 2 ? '' : 's'}` : 'the minidump';
}

/**
 * The stack walk of one dump: its options, cached result and runs. Owned by
 * the detail view so the header's refresh button and the panel share it.
 * `dump` is null until the dump is read from the browser storage.
 */
function useStackwalk(minidump: Minidump, dump: Blob | null) {
  const [view, setView] = useState<'frames' | 'text'>('frames');
  const [options, setOptions] = useState<Omit<StackwalkOptions, 'format'>>({
    stack: false,
    crashOnly: false,
    brief: false,
    dumpPointers: false,
    thread: null,
  });
  // The last stack walk, from the browser cache; only refresh asks the server.
  const [cached, setCached] = useState<CachedStackwalk | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  // Upload progress of the dump and its symbols, 0..1, while a run is busy.
  const [upload, setUpload] = useState(0);
  // This dump's own symbol files; all of them go in the same request as the dump.
  const { symbols, error: symbolsError } = useDumpSymbols(minidump.id);

  useEffect(() => {
    let cancelled = false;
    getCachedStackwalk(minidump.id)
      .then((entry) => {
        if (cancelled || !entry) return;
        setCached(entry);
        // Show the controls the cached run used.
        const { format, ...rest } = entry.options;
        setView(format === 'machine' ? 'frames' : 'text');
        setOptions(rest);
      })
      .catch((cause) => !cancelled && setError(`Cannot read the cached stack walk: ${errorMessage(cause)}`))
      .finally(() => !cancelled && setLoading(false));
    return () => {
      cancelled = true;
    };
  }, [minidump.id]);

  /** Walks the stack with the current options and symbols, and replaces the cached result. */
  const execute = async () => {
    if (dump === null || symbols === null) return;
    setBusy(true);
    setUpload(0);
    setError(null);
    const runOptions: StackwalkOptions = { ...options, format: view === 'frames' ? 'machine' : 'human' };
    const keep = async (result: ToolResult) => {
      const entry: CachedStackwalk = { minidumpId: minidump.id, options: runOptions, result, ran: Date.now() };
      setCached(entry);
      try {
        await cacheStackwalk(entry);
      } catch (cause) {
        setError(`The result could not be cached: ${errorMessage(cause)}`);
      }
    };
    try {
      await keep((await runStackwalk(dump, minidump.name, runOptions, symbols, setUpload)).result);
    } catch (cause) {
      setError(errorMessage(cause));
      // A tool that ran and failed still answered; its output replaces the cache too.
      if (cause instanceof ApiError && cause.result) await keep(cause.result);
    } finally {
      setBusy(false);
    }
  };

  // A dump with nothing cached (just dropped in) is walked once on its own;
  // after that only refresh asks the server.
  const autoRan = useRef(false);
  useEffect(() => {
    if (autoRan.current || loading || cached || error || dump === null || symbols === null) return;
    autoRan.current = true;
    execute();
  });

  return { view, setView, options, setOptions, cached, loading, error, busy, upload, symbols, symbolsError, execute };
}

type Stackwalk = ReturnType<typeof useStackwalk>;

function StackPanel({ minidump, walk }: { minidump: Minidump; walk: Stackwalk }) {
  const { view, setView, options, setOptions, cached, loading, error, busy, symbols, symbolsError } = walk;
  const set = <K extends keyof typeof options>(key: K) => (value: (typeof options)[K]) =>
    setOptions((current) => ({ ...current, [key]: value }));

  // Symbol files dropped anywhere on the panel go to this dump's list.
  const adder = useSymbolAdder((blob) => addDumpSymbol(minidump.id, blob));
  const { dragging, dropProps } = useFileDrop(adder.add);

  return (
    <div className={`panel${dragging ? ' drop-active' : ''}`} {...dropProps}>
      <div className="toolbar">
        <div className="segmented">
          <button type="button" className={view === 'frames' ? 'active' : ''} onClick={() => setView('frames')}>
            Frames
          </button>
          <button type="button" className={view === 'text' ? 'active' : ''} onClick={() => setView('text')}>
            Text
          </button>
        </div>
        <Checkbox label="Crashing thread only" checked={options.crashOnly} onChange={set('crashOnly')} hint="-c" />
        {view === 'text' ? (
          <>
            <Checkbox label="Stack contents" checked={options.stack} onChange={set('stack')} hint="-s" />
            <Checkbox label="Brief" checked={options.brief} onChange={set('brief')} hint="-b" />
            <Checkbox label="Pointers on stack" checked={options.dumpPointers} onChange={set('dumpPointers')} hint="-d" />
          </>
        ) : null}
        <label className="inline-field" title="-t">
          Thread
          <input
            type="number"
            min={0}
            placeholder="all"
            value={options.thread ?? ''}
            onChange={(event) => set('thread')(event.target.value === '' ? null : Number(event.target.value))}
          />
        </label>
      </div>

      <SymbolList
        symbols={symbols}
        error={symbolsError}
        emptyText="No symbols for this minidump yet."
        onAdd={(blob) => addDumpSymbol(minidump.id, blob)}
        onRemove={(key) => removeDumpSymbol(minidump.id, key)}
        adder={adder}
      />

      {!busy && !loading && !cached && !error ? <p className="muted">Not run yet. Press ↻ to walk the stack.</p> : null}
      {cached ? (
        <StackwalkResult
          tool="minidump_stackwalk"
          result={cached.result}
          frames={cached.options.format === 'machine'}
          downloadName={`${minidump.name}.stackwalk.psv.txt`}
        />
      ) : null}
    </div>
  );
}

interface MinidumpDetailProps {
  minidump: Minidump;
}

const SOURCE_LABELS: Record<Minidump['source'], string> = {
  file: 'opened from a file',
  pid2md: 'captured with pid2md',
  core2md: 'converted from a core file',
};

export default function MinidumpDetail({ minidump }: MinidumpDetailProps) {
  const [dump, setDump] = useState<Blob | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    getMinidumpBlob(minidump.id)
      .then((blob) => {
        if (cancelled) return;
        if (blob) setDump(blob);
        else setError('This minidump is no longer in the browser storage.');
      })
      .catch((cause) => !cancelled && setError(errorMessage(cause)));
    return () => {
      cancelled = true;
    };
  }, [minidump.id]);

  const walk = useStackwalk(minidump, dump);
  const runLabel = walk.busy ? 'Running…' : walk.cached ? 'Run again' : 'Run';

  return (
    <article className="detail">
      <header className="detail-header">
        <div>
          <h2>{minidump.name}</h2>
        </div>
        <div className="actions">
          <button type="button" disabled={!dump} onClick={() => dump && saveBlob(dump, minidump.name)}>
            Download
          </button>
          <button
            type="button"
            className="icon-button"
            onClick={walk.execute}
            disabled={!dump || walk.busy || walk.symbols === null}
            title={runLabel}
            aria-label={runLabel}
          >
            <RefreshIcon />
          </button>
        </div>
      </header>
      {walk.busy ? <UploadProgressLine upload={walk.upload} uploading={`Uploading ${uploadWhat(1 + (walk.symbols?.length ?? 0))}`} working="Walking the stack…" /> : null}
      {error ? <p className="error">{error}</p> : null}
      {walk.error ? <p className="error">{walk.error}</p> : null}

      <dl className="facts">
        {minidump.product ? (
          <div>
            <dt>Product</dt>
            <dd>
              {minidump.product} {minidump.version ? <span className="muted">{minidump.version}</span> : null}
            </dd>
          </div>
        ) : null}
        <div>
          <dt>Size</dt>
          <dd>{formatBytes(minidump.size)}</dd>
        </div>
        <div>
          <dt>Added</dt>
          <dd>{formatDate(minidump.created)}</dd>
        </div>
        <div>
          <dt>Source</dt>
          <dd>
            {SOURCE_LABELS[minidump.source] ?? minidump.source}
            {minidump.pid ? <span className="muted"> · pid {minidump.pid}</span> : null}
          </dd>
        </div>
      </dl>

      {dump ? <StackPanel minidump={minidump} walk={walk} /> : null}
    </article>
  );
}
