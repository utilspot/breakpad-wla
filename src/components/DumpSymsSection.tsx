import { useEffect, useRef, useState, type FormEvent } from 'react';
import { ApiError, dumpSymbols } from '../api';
import { errorMessage, formatBytes, saveBlob } from '../format';
import { addDumpSymbol } from '../dumpSymbolStore';
import { MINIDUMPS_CHANGED, listMinidumps } from '../minidumpStore';
import type { DumpSymsOptions, Minidump, ToolResult } from '../types';
import DropZone from './DropZone';
import SymbolText from './SymbolText';
import ToolOutput from './ToolOutput';
import UploadProgressLine from './UploadProgressLine';

/** Record types of a Breakpad symbol file, in the order they are listed. */
const RECORD_TYPES = ['FILE', 'FUNC', 'PUBLIC', 'INLINE', 'INLINE_ORIGIN', 'STACK CFI', 'STACK WIN'] as const;
/** Longest prefix first, so "INLINE_ORIGIN" is not counted as "INLINE". */
const PREFIXES = [...RECORD_TYPES].sort((a, b) => b.length - a.length).map((type) => [type, `${type} `] as const);

interface Symbols {
  blob: Blob;
  filename: string;
  /** Fields of the `MODULE <os> <arch> <id> <name>` line. */
  module: { os: string; arch: string; id: string; name: string };
  counts: Record<(typeof RECORD_TYPES)[number], number>;
  /** Every line of the file, all shown. */
  lines: string[];
}

async function summarize(blob: Blob, filename: string): Promise<Symbols> {
  const text = await blob.text();
  const lines = text.split('\n');
  if (lines[lines.length - 1] === '') lines.pop();

  // The name ends at the first space; anything after it (e.g. a version) is not part of it.
  const [, os = '', arch = '', id = '', name = ''] = (lines[0] ?? '').split(' ');
  const counts = Object.fromEntries(RECORD_TYPES.map((type) => [type, 0])) as Symbols['counts'];
  for (const line of lines) {
    const match = PREFIXES.find(([, prefix]) => line.startsWith(prefix));
    if (match) counts[match[0]] += 1;
  }

  return {
    blob,
    filename,
    module: { os, arch, id, name },
    counts,
    lines,
  };
}

function Checkbox({ label, flag, checked, onChange }: { label: string; flag: string; checked: boolean; onChange: (value: boolean) => void }) {
  return (
    <label className="check" title={flag}>
      <input type="checkbox" checked={checked} onChange={(event) => onChange(event.target.checked)} />
      {label}
    </label>
  );
}

/**
 * Turns an ELF file into a Breakpad symbol file with dump_syms and hands it
 * back for preview and download. The server keeps nothing; the user may add
 * the result to one of the minidumps in this browser.
 */
export default function DumpSymsSection() {
  const [binary, setBinary] = useState<File | null>(null);
  const [debug, setDebug] = useState<File | null>(null);
  const [options, setOptions] = useState<DumpSymsOptions>({
    cfi: true,
    inline: false,
    headerOnly: false,
    noCrossCu: false,
    preserveLoadAddress: false,
    multiple: false,
    verbose: false,
    id: '',
    name: '',
    os: '',
  });
  const [symbols, setSymbols] = useState<Symbols | null>(null);
  const [failure, setFailure] = useState<{ error: string; result?: ToolResult } | null>(null);
  const [busy, setBusy] = useState(false);
  /** Upload progress of the ELF file (and debug file), 0..1, while dump_syms runs. */
  const [upload, setUploadProgress] = useState(0);
  const [stored, setStored] = useState<string | null>(null);
  const [minidumps, setMinidumps] = useState<Minidump[]>([]);
  const [target, setTarget] = useState('');

  useEffect(() => {
    const load = () => listMinidumps().then(setMinidumps, () => setMinidumps([]));
    load();
    window.addEventListener(MINIDUMPS_CHANGED, load);
    return () => window.removeEventListener(MINIDUMPS_CHANGED, load);
  }, []);

  const set = <K extends keyof DumpSymsOptions>(key: K) => (value: DumpSymsOptions[K]) =>
    setOptions((current) => ({ ...current, [key]: value }));

  // Counts runs, so a slower earlier run cannot overwrite the result of a newer one.
  const latestRun = useRef(0);

  /** Runs dump_syms and replaces whatever the previous run showed. */
  const run = async (file: File, debugFile: File | null) => {
    const current = ++latestRun.current;
    setBusy(true);
    setUploadProgress(0);
    setSymbols(null);
    setFailure(null);
    setStored(null);
    try {
      const { blob, filename } = await dumpSymbols(file, debugFile, options, (fraction) => {
        if (current === latestRun.current) setUploadProgress(fraction);
      });
      const summary = await summarize(blob, filename);
      if (current === latestRun.current) setSymbols(summary);
    } catch (error) {
      if (current === latestRun.current) {
        setFailure({ error: errorMessage(error), result: error instanceof ApiError ? error.result : undefined });
      }
    } finally {
      if (current === latestRun.current) setBusy(false);
    }
  };

  const submit = (event: FormEvent) => {
    event.preventDefault();
    if (binary) run(binary, debug);
  };

  /**
   * A new ELF file replaces the previous one and its result right away. A debug
   * file picked for the previous binary does not belong to this one, so it goes too.
   */
  const pickBinary = (file: File | undefined) => {
    if (!file) return;
    const debugFile = binary ? null : debug;
    setBinary(file);
    setDebug(debugFile);
    run(file, debugFile);
  };

  const store = async () => {
    const minidump = minidumps.find((item) => item.id === target);
    if (!symbols || !minidump) return;
    try {
      await addDumpSymbol(minidump.id, symbols.blob);
      setStored(minidump.name);
    } catch (error) {
      setFailure({ error: errorMessage(error) });
    }
  };

  return (
    <section>
      <h2>Symbols from an ELF file</h2>
      <p className="muted">
        Runs <code>dump_syms</code> on an executable or shared library and returns its Breakpad symbol file. The file is
        not kept on the server: preview it, download it, or add it to a minidump in this browser.
      </p>

      <form onSubmit={submit}>
        <div className="field-row">
          <div className="grow">
            <DropZone
              label={binary ? binary.name : 'ELF file'}
              hint={binary ? formatBytes(binary.size) : 'Executable or shared library, ideally built with -g'}
              onFiles={(files) => pickBinary(files[0])}
            />
          </div>
          <div className="grow">
            <DropZone
              label={debug ? debug.name : 'Separate debug file (optional)'}
              hint={
                debug
                  ? formatBytes(debug.size)
                  : "When the debug info was split off with objcopy --only-keep-debug. It must keep the name the binary's .gnu_debuglink points to."
              }
              onFiles={(files) => setDebug(files[0] ?? null)}
            />
            {debug ? (
              <button type="button" className="button-small" onClick={() => setDebug(null)}>
                Remove debug file
              </button>
            ) : null}
          </div>
        </div>

        <div className="toolbar">
          <Checkbox label="Call frame info" flag="unchecked: -c" checked={options.cfi} onChange={set('cfi')} />
          <Checkbox label="Inlined functions" flag="-d" checked={options.inline} onChange={set('inline')} />
          <Checkbox label="Header only" flag="-i" checked={options.headerOnly} onChange={set('headerOnly')} />
          <Checkbox label="Skip cross-CU references" flag="-r" checked={options.noCrossCu} onChange={set('noCrossCu')} />
          <Checkbox label="Keep load address" flag="-a" checked={options.preserveLoadAddress} onChange={set('preserveLoadAddress')} />
          <Checkbox label="Mark multiple symbols" flag="-m" checked={options.multiple} onChange={set('multiple')} />
          <Checkbox label="All warnings" flag="-v" checked={options.verbose} onChange={set('verbose')} />
        </div>
        <details>
          <summary>Override module fields</summary>
          <div className="field-row">
            <label className="field">
              Module id <span className="muted">(-b)</span>
              <input type="text" placeholder="from the build id" value={options.id} onChange={(event) => set('id')(event.target.value)} />
            </label>
            <label className="field">
              Module name <span className="muted">(-n)</span>
              <input type="text" placeholder="from the file name" value={options.name} onChange={(event) => set('name')(event.target.value)} />
            </label>
            <label className="field">
              OS <span className="muted">(-o)</span>
              <input type="text" placeholder="Linux" value={options.os} onChange={(event) => set('os')(event.target.value)} />
            </label>
          </div>
        </details>

        <div className="toolbar">
          <div className="spacer" />
          <button type="submit" disabled={!binary || busy}>
            {busy ? 'Running dump_syms…' : 'Get symbols'}
          </button>
        </div>
      </form>

      {busy ? (
        <UploadProgressLine
          upload={upload}
          uploading={debug ? `Uploading ${binary?.name} and its debug file` : `Uploading ${binary?.name}`}
          working="Running dump_syms…"
        />
      ) : null}
      {failure ? <p className="error">{failure.error}</p> : null}
      {failure?.result ? <ToolOutput result={failure.result} saveAs="dump_syms" /> : null}

      {symbols ? (
        <div className="symbols-result">
          <dl className="facts">
            <div>
              <dt>Module</dt>
              <dd className="mono">{symbols.module.name}</dd>
            </div>
            <div>
              <dt>Debug id</dt>
              <dd className="mono">{symbols.module.id}</dd>
            </div>
            <div>
              <dt>OS / arch</dt>
              <dd>
                {symbols.module.os} {symbols.module.arch}
              </dd>
            </div>
            <div>
              <dt>Size</dt>
              <dd>
                {formatBytes(symbols.blob.size)} <span className="muted">· {symbols.lines.length.toLocaleString()} lines</span>
              </dd>
            </div>
          </dl>
          <p className="record-counts">
            {RECORD_TYPES.filter((type) => symbols.counts[type] > 0).map((type) => (
              <span key={type} className="badge">
                {type} {symbols.counts[type].toLocaleString()}
              </span>
            ))}
          </p>
          <div className="toolbar">
            <button type="button" onClick={() => saveBlob(symbols.blob, symbols.filename)}>
              Download {symbols.filename}
            </button>
            <select
              value={target}
              onChange={(event) => {
                setTarget(event.target.value);
                setStored(null);
              }}
              disabled={!minidumps.length}
            >
              <option value="">{minidumps.length ? 'Choose a minidump…' : 'No minidumps in this browser'}</option>
              {minidumps.map((item) => (
                <option key={item.id} value={item.id}>
                  {item.name}
                </option>
              ))}
            </select>
            <button type="button" onClick={store} disabled={!target || stored !== null}>
              Add to minidump
            </button>
            {stored ? <span className="success">Added to {stored}</span> : null}
          </div>
          <SymbolText lines={symbols.lines} />
        </div>
      ) : null}
    </section>
  );
}
