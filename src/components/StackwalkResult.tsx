import type { ToolResult } from '../types';
import StackwalkView from './StackwalkView';
import { statusOf } from './ToolOutput';

/**
 * A stack walk's answer: the frames when it ran with -m and they parse, the
 * plain text otherwise, and a line saying so when the tool did not succeed.
 */
export default function StackwalkResult({
  tool,
  result,
  frames,
  downloadName,
}: {
  tool: string;
  result: ToolResult;
  frames: boolean;
  /** File name for downloading the raw frames output. */
  downloadName?: string;
}) {
  const status = statusOf(result);
  return (
    <>
      {!result.ok ? <p className="error">
          {tool} did not succeed: {status.label}.
        </p> : null}
      {result.stdoutTruncated ? <p className="notice">Output was cut off at the server's size limit.</p> : null}
      {frames && result.stdout.startsWith('OS|') ? (
        <StackwalkView output={result.stdout} downloadName={downloadName} />
      ) : result.stdout ? (
        <pre className="output">{result.stdout}</pre>
      ) : null}
    </>
  );
}
