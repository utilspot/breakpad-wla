import { useState, type FormEvent } from 'react';
import { ApiError, PROC_FILES, captureProcess, convertCoreToMinidump, type ProcFile } from '../api';
import { errorMessage } from '../format';
import { addMinidump } from '../minidumpStore';
import { hrefFor } from '../router';
import type { Minidump, ToolResult } from '../types';
import ToolOutput from './ToolOutput';

interface Outcome {
  minidump?: Minidump;
  result?: ToolResult;
  error?: string;
}

/** Keeps a captured minidump in this browser; the server has already deleted its copy. */
async function keep(capture: Promise<{ blob: Blob; filename: string }>, details: Pick<Minidump, 'source' | 'pid'>): Promise<Outcome> {
  const { blob, filename } = await capture;
  return { minidump: await addMinidump(blob, { name: filename, ...details }) };
}

function outcomeFromError(error: unknown): Outcome {
  return { error: errorMessage(error), result: error instanceof ApiError ? error.result : undefined };
}

function OutcomeView({ outcome, saveAs }: { outcome: Outcome; saveAs: string }) {
  return (
    <>
      {outcome.minidump ? (
        <p className="success">
          Kept in this browser as <a href={hrefFor({ page: 'minidumps', id: outcome.minidump.id })}>{outcome.minidump.name}</a>.
        </p>
      ) : null}
      {outcome.error ? <p className="error">{outcome.error}</p> : null}
      {outcome.result ? <ToolOutput result={outcome.result} saveAs={saveAs} /> : null}
    </>
  );
}

function ProcessCapture() {
  const [pid, setPid] = useState('');
  const [outcome, setOutcome] = useState<Outcome | null>(null);
  const [busy, setBusy] = useState(false);

  const submit = async (event: FormEvent) => {
    event.preventDefault();
    setBusy(true);
    try {
      setOutcome(await keep(captureProcess(Number(pid)), { source: 'pid2md', pid: Number(pid) }));
    } catch (error) {
      setOutcome(outcomeFromError(error));
    } finally {
      setBusy(false);
    }
  };

  return (
    <form className="panel" onSubmit={submit}>
      <h2>Running process</h2>
      <p className="muted">
        <code>pid2md</code> writes a minidump of a live process on the <em>server</em> machine; the process keeps running
        afterwards. It needs permission to ptrace the process: the server runs as the same user and{' '}
        <code>/proc/sys/kernel/yama/ptrace_scope</code> is 0, or the server has <code>CAP_SYS_PTRACE</code>.
      </p>
      <div className="toolbar">
        <label className="inline-field">
          Process id
          <input type="number" min={1} required value={pid} onChange={(event) => setPid(event.target.value)} />
        </label>
        <div className="spacer" />
        <button type="submit" disabled={busy || !pid}>
          {busy ? 'Capturing…' : 'Capture minidump'}
        </button>
      </div>
      {outcome ? <OutcomeView outcome={outcome} saveAs={`pid2md-${pid}`} /> : null}
    </form>
  );
}

function CoreCapture() {
  const [core, setCore] = useState<File | null>(null);
  const [procFiles, setProcFiles] = useState<Partial<Record<ProcFile, File>>>({});
  const [progress, setProgress] = useState<number | null>(null);
  const [outcome, setOutcome] = useState<Outcome | null>(null);

  const submit = async (event: FormEvent) => {
    event.preventDefault();
    if (!core) return;
    setProgress(0);
    try {
      setOutcome(await keep(convertCoreToMinidump(core, procFiles, setProgress), { source: 'core2md' }));
    } catch (error) {
      setOutcome(outcomeFromError(error));
    } finally {
      setProgress(null);
    }
  };

  return (
    <form className="panel" onSubmit={submit}>
      <h2>Core file</h2>
      <p className="muted">
        <code>core2md</code> turns a core file into a minidump. Besides the core it needs the files the process had in{' '}
        <code>/proc/&lt;pid&gt;</code>, copied while it was still alive — for instance from a <code>core_pattern</code>{' '}
        handler. Missing ones leave gaps in the minidump or make the conversion fail.
      </p>
      <div className="file-grid">
        <label className="field">
          Core file
          <input type="file" required onChange={(event) => setCore(event.target.files?.[0] ?? null)} />
        </label>
        {PROC_FILES.map((name) => (
          <label key={name} className="field">
            <span className="mono">/proc/&lt;pid&gt;/{name}</span>
            <input
              type="file"
              onChange={(event) => {
                const file = event.target.files?.[0];
                setProcFiles((current) => ({ ...current, [name]: file }));
              }}
            />
          </label>
        ))}
      </div>
      <div className="toolbar">
        {progress !== null ? <span className="muted">Uploading… {Math.round(progress * 100)}%</span> : null}
        <div className="spacer" />
        <button type="submit" disabled={!core || progress !== null}>
          Convert to minidump
        </button>
      </div>
      {outcome ? <OutcomeView outcome={outcome} saveAs="core2md" /> : null}
    </form>
  );
}

export default function CapturePage() {
  return (
    <div className="page-body two-columns">
      <ProcessCapture />
      <CoreCapture />
    </div>
  );
}
