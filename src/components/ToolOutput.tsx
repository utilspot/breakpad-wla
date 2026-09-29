import { useState } from 'react';
import { formatDuration, saveBlob } from '../format';
import type { ToolResult } from '../types';

export function statusOf(result: ToolResult): { label: string; tone: 'ok' | 'warn' | 'error' } {
  if (result.spawnError) return { label: `Could not start: ${result.spawnError}`, tone: 'error' };
  if (result.timedOut) return { label: 'Timed out', tone: 'error' };
  if (result.signal) return { label: `Killed by ${result.signal}`, tone: 'error' };
  if (result.exitCode === 0) return { label: 'Exit 0', tone: 'ok' };
  return { label: `Exit ${result.exitCode}`, tone: 'warn' };
}

interface ToolOutputProps {
  result: ToolResult;
  /** Base name for the saved output, e.g. `stackwalk`. */
  saveAs?: string;
}

/** The command, exit status and stdout of one tool run; the server does not send stderr. */
export default function ToolOutput({ result, saveAs = 'output' }: ToolOutputProps) {
  const [copied, setCopied] = useState(false);
  const status = statusOf(result);
  const text = result.stdout;
  const truncated = result.stdoutTruncated;

  const copy = async () => {
    try {
      await navigator.clipboard.writeText(text);
      setCopied(true);
      setTimeout(() => setCopied(false), 1500);
    } catch {
      // Clipboard access denied (e.g. plain http on another host); nothing to do.
    }
  };

  return (
    <section className="tool-output">
      <header className="tool-output-header">
        <span className={`badge badge-${status.tone}`}>{status.label}</span>
        <span className="muted">{formatDuration(result.durationMs)}</span>
        <code className="command" title={result.command}>
          {result.command}
        </code>
      </header>
      <div className="tool-output-bar">
        <div className="spacer" />
        <button type="button" className="button-small" onClick={copy} disabled={!text}>
          {copied ? 'Copied' : 'Copy'}
        </button>
        <button
          type="button"
          className="button-small"
          disabled={!text}
          onClick={() => saveBlob(new Blob([text], { type: 'text/plain' }), `${saveAs}.txt`)}
        >
          Save
        </button>
      </div>
      {truncated ? <p className="notice">Output was cut off at the server's size limit.</p> : null}
      <pre className="output">{text || <span className="muted">No output.</span>}</pre>
    </section>
  );
}
