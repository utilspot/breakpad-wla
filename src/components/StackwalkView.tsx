import { useMemo } from 'react';
import { saveBlob } from '../format';
import { baseName, parseMachineStackwalk, type Frame, type Stackwalk, type Thread } from '../stackwalk';

function FrameRow({ frame }: { frame: Frame }) {
  const symbolized = frame.function !== '';
  return (
    <tr className={symbolized ? '' : 'unsymbolized'}>
      <td className="num">{frame.index}</td>
      <td className="mono">{frame.module || <span className="muted">?</span>}</td>
      <td className="mono function">
        {symbolized ? frame.function : <span className="muted">{frame.module ? `${frame.module} + ${frame.offset}` : frame.offset}</span>}
      </td>
      <td className="mono source" title={frame.file}>
        {frame.file ? `${baseName(frame.file)}:${frame.line ?? '?'}` : ''}
      </td>
      <td className="mono muted">{symbolized ? `+${frame.offset}` : ''}</td>
    </tr>
  );
}

function ThreadTable({ thread, crashed }: { thread: Thread; crashed: boolean }) {
  return (
    <details className="thread" open={crashed}>
      <summary>
        Thread {thread.index}
        {crashed ? <span className="badge badge-error">crashed</span> : null}
        <span className="muted"> {thread.frames.length} frames</span>
        {thread.frames[0]?.function ? <span className="mono muted"> · {thread.frames[0].function}</span> : null}
      </summary>
      <table className="frames">
        <thead>
          <tr>
            <th className="num">#</th>
            <th>Module</th>
            <th>Function</th>
            <th>Source</th>
            <th>Offset</th>
          </tr>
        </thead>
        <tbody>
          {thread.frames.map((frame) => (
            <FrameRow key={frame.index} frame={frame} />
          ))}
        </tbody>
      </table>
    </details>
  );
}

/** Modules that appear in a stack without function names: their symbols are missing. */
function modulesMissingSymbols(stackwalk: Stackwalk) {
  const names = new Set<string>();
  for (const thread of stackwalk.threads) {
    for (const frame of thread.frames) {
      if (frame.module && !frame.function) names.add(frame.module);
    }
  }
  return stackwalk.modules.filter((module) => names.has(baseName(module.codeFile)));
}

/** Structured view of `minidump_stackwalk -f machine` output. */
/** `downloadName` is the file name the Result item's Download button saves the raw output under. */
export default function StackwalkView({ output, downloadName = 'stackwalk.psv.txt' }: { output: string; downloadName?: string }) {
  const stackwalk = useMemo(() => parseMachineStackwalk(output), [output]);
  const missing = useMemo(() => modulesMissingSymbols(stackwalk), [stackwalk]);

  return (
    <div className="stackwalk">
      <dl className="summary">
        <div>
          <dt>Crash</dt>
          <dd className={stackwalk.crashReason ? 'crash' : ''}>{stackwalk.crashReason || 'No crash recorded'}</dd>
        </div>
        {stackwalk.crashAddress ? (
          <div>
            <dt>Address</dt>
            <dd className="mono">{stackwalk.crashAddress}</dd>
          </div>
        ) : null}
        <div>
          <dt>OS</dt>
          <dd>
            {stackwalk.os} <span className="muted">{stackwalk.osVersion}</span>
          </dd>
        </div>
        <div>
          <dt>CPU</dt>
          <dd>
            {stackwalk.cpu} <span className="muted">{stackwalk.cpuInfo}</span>
          </dd>
        </div>
      </dl>

      {missing.length ? (
        <div className="notice">
          <strong>Missing symbols</strong> for {missing.length === 1 ? 'this module' : 'these modules'}. Run{' '}
          <code>dump_syms</code> on the matching binaries, or add them with <em>Add symbols</em>:
          <ul className="missing">
            {missing.map((module) => (
              <li key={`${module.debugFile}/${module.debugId}`} className="mono">
                {module.debugFile || baseName(module.codeFile)} <span className="muted">{module.debugId}</span>
              </li>
            ))}
          </ul>
        </div>
      ) : null}

      {stackwalk.threads.length ? (
        stackwalk.threads.map((thread) => (
          <ThreadTable key={thread.index} thread={thread} crashed={thread.index === stackwalk.crashedThread} />
        ))
      ) : (
        <p className="muted">No stack frames in the output.</p>
      )}

      {stackwalk.modules.length ? (
        <details className="modules">
          <summary>
            Loaded modules <span className="muted">{stackwalk.modules.length}</span>
          </summary>
          <table>
            <thead>
              <tr>
                <th>Code file</th>
                <th>Version</th>
                <th>Debug file</th>
                <th>Debug id</th>
                <th>Address range</th>
              </tr>
            </thead>
            <tbody>
              {stackwalk.modules.map((module) => (
                <tr key={`${module.base}-${module.codeFile}`}>
                  <td className="mono">
                    {baseName(module.codeFile)}
                    {module.isMain ? <span className="badge">main</span> : null}
                  </td>
                  <td className="mono">{module.version}</td>
                  <td className="mono">{module.debugFile}</td>
                  <td className="mono">{module.debugId}</td>
                  <td className="mono muted">
                    {module.base}–{module.end}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </details>
      ) : null}

      <details className="modules">
        <summary>
          Result{' '}
          <button
            type="button"
            className="button-small summary-action"
            onClick={(event) => {
              // Download without opening or closing the item.
              event.preventDefault();
              saveBlob(new Blob([output], { type: 'text/plain' }), downloadName);
            }}
          >
            Download
          </button>
        </summary>
        <pre className="output">{output}</pre>
      </details>
    </div>
  );
}
