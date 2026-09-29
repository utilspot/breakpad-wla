import { useState, type FormEvent } from 'react';
import { ApiError, runMicrodumpStackwalk, type SymbolAttachment } from '../api';
import { errorMessage } from '../format';
import { toAttachment } from '../symbolFile';
import type { ToolResult } from '../types';
import DropZone from './DropZone';
import SymbolList from './SymbolList';
import StackwalkResult from './StackwalkResult';

export default function MicrodumpPage() {
  const [text, setText] = useState('');
  const [file, setFile] = useState<File | null>(null);
  const [view, setView] = useState<'frames' | 'text'>('frames');
  const [stack, setStack] = useState(false);
  const [result, setResult] = useState<ToolResult | null>(null);
  const [resultView, setResultView] = useState<'frames' | 'text'>('frames');
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  // Kept only while the page is open; all of them go in the same request as the microdump.
  const [symbols, setSymbols] = useState<SymbolAttachment[]>([]);

  const addSymbol = async (blob: Blob) => {
    const added = await toAttachment(blob);
    setSymbols((current) =>
      [...current.filter(({ symbol }) => symbol.key !== added.symbol.key), added].sort((a, b) => a.symbol.name.localeCompare(b.symbol.name)),
    );
  };

  const submit = async (event: FormEvent) => {
    event.preventDefault();
    setBusy(true);
    setError(null);
    try {
      const input = file ? { file } : { text };
      const response = await runMicrodumpStackwalk(input, { machine: view === 'frames', stack }, symbols);
      setResult(response.result);
      setResultView(view);
    } catch (cause) {
      setError(errorMessage(cause));
      setResult(cause instanceof ApiError ? (cause.result ?? null) : null);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="page-body">
      <form className="panel" onSubmit={submit}>
        <p className="muted">
          Android writes microdumps into logcat between <code>-----BEGIN BREAKPAD MICRODUMP-----</code> and{' '}
          <code>-----END BREAKPAD MICRODUMP-----</code>. Paste the log, or upload it, and{' '}
          <code>microdump_stackwalk</code> symbolizes it with the symbol files added below, sent along with it.
        </p>
        <div className="microdump-input">
          <label className="field grow">
            Paste
            <textarea
              rows={10}
              spellCheck={false}
              placeholder="-----BEGIN BREAKPAD MICRODUMP-----"
              value={text}
              disabled={file !== null}
              onChange={(event) => setText(event.target.value)}
            />
          </label>
          <div className="or">or</div>
          <div className="microdump-file">
            <DropZone label="Upload a log file" onFiles={(files) => setFile(files[0] ?? null)} />
            {file ? (
              <p>
                <span className="mono">{file.name}</span>{' '}
                <button type="button" className="button-small" onClick={() => setFile(null)}>
                  Remove
                </button>
              </p>
            ) : null}
          </div>
        </div>
        <div className="toolbar">
          <div className="segmented">
            <button type="button" className={view === 'frames' ? 'active' : ''} onClick={() => setView('frames')}>
              Frames
            </button>
            <button type="button" className={view === 'text' ? 'active' : ''} onClick={() => setView('text')}>
              Text
            </button>
          </div>
          {view === 'text' ? (
            <label className="check" title="-s">
              <input type="checkbox" checked={stack} onChange={(event) => setStack(event.target.checked)} />
              Stack contents
            </label>
          ) : null}
          <div className="spacer" />
          <button type="submit" disabled={busy || (!file && !text.trim())}>
            {busy ? 'Walking…' : 'Walk the stack'}
          </button>
        </div>
      </form>

      <SymbolList
        symbols={symbols}
        emptyText="No symbols added."
        onAdd={addSymbol}
        onRemove={(key) => setSymbols((current) => current.filter(({ symbol }) => symbol.key !== key))}
      />

      {error ? <p className="error">{error}</p> : null}
      {result ? <StackwalkResult tool="microdump_stackwalk" result={result} frames={resultView === 'frames'} downloadName="microdump.stackwalk.psv.txt" /> : null}
    </div>
  );
}
