/**
 * Parser for `minidump_stackwalk -f machine` / `microdump_stackwalk -m`
 * output: pipe separated lines, see PrintProcessStateMachineReadable in
 * breakpad's src/processor/stackwalk_common.cc.
 *
 *   OS|<os>|<version>
 *   CPU|<arch>|<info>|<count>
 *   Crash|<reason>|<address>|<crashed thread>
 *   Module|<code file>|<version>|<debug file>|<debug id>|<base>|<end>|<is main>
 *   <thread>|<frame>|<module>|<function>|<source file>|<line>|<offset>
 */

export interface Frame {
  index: number;
  module: string;
  function: string;
  file: string;
  line: number | null;
  offset: string;
}

export interface Thread {
  index: number;
  frames: Frame[];
}

export interface CodeModule {
  codeFile: string;
  version: string;
  debugFile: string;
  debugId: string;
  base: string;
  end: string;
  isMain: boolean;
}

export interface Stackwalk {
  os: string;
  osVersion: string;
  cpu: string;
  cpuInfo: string;
  crashReason: string;
  crashAddress: string;
  crashedThread: number | null;
  modules: CodeModule[];
  threads: Thread[];
}

export function parseMachineStackwalk(text: string): Stackwalk {
  const result: Stackwalk = {
    os: '',
    osVersion: '',
    cpu: '',
    cpuInfo: '',
    crashReason: '',
    crashAddress: '',
    crashedThread: null,
    modules: [],
    threads: [],
  };
  const threads = new Map<number, Thread>();

  for (const line of text.split('\n')) {
    if (!line) continue;
    const fields = line.split('|');
    switch (fields[0]) {
      case 'OS':
        result.os = fields[1] ?? '';
        result.osVersion = fields[2] ?? '';
        break;
      case 'CPU':
        result.cpu = fields[1] ?? '';
        result.cpuInfo = fields[2] ?? '';
        break;
      case 'GPU':
        break;
      case 'Crash':
        result.crashReason = fields[1] ?? '';
        result.crashAddress = fields[2] ?? '';
        result.crashedThread = fields[3] ? Number(fields[3]) : null;
        break;
      case 'Module':
        result.modules.push({
          codeFile: fields[1] ?? '',
          version: fields[2] ?? '',
          debugFile: fields[3] ?? '',
          debugId: fields[4] ?? '',
          base: fields[5] ?? '',
          end: fields[6] ?? '',
          isMain: fields[7] === '1',
        });
        break;
      default: {
        const threadIndex = Number(fields[0]);
        const frameIndex = Number(fields[1]);
        if (fields.length < 7 || !Number.isInteger(threadIndex) || !Number.isInteger(frameIndex)) break;
        let thread = threads.get(threadIndex);
        if (!thread) {
          thread = { index: threadIndex, frames: [] };
          threads.set(threadIndex, thread);
        }
        thread.frames.push({
          index: frameIndex,
          module: fields[2],
          function: fields[3],
          file: fields[4],
          line: fields[5] ? Number(fields[5]) : null,
          offset: fields[6],
        });
      }
    }
  }

  // The crashed thread first, the rest in order.
  result.threads = [...threads.values()].sort((a, b) => {
    if (a.index === result.crashedThread) return -1;
    if (b.index === result.crashedThread) return 1;
    return a.index - b.index;
  });
  return result;
}

/** Last path component, for both / and \ separated paths. */
export const baseName = (path: string): string => path.split(/[\\/]/).pop() ?? path;
