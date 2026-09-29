/** One run of a breakpad tool on the server. */
export interface ToolResult {
  command: string;
  ok: boolean;
  /** Present only when the tool could not be started at all. */
  spawnError?: string;
  /** null when the tool did not exit on its own (signal, timeout, spawn error). */
  exitCode: number | null;
  signal: string | null;
  timedOut: boolean;
  durationMs: number;
  stdout: string;
  stdoutTruncated: boolean;
}

export interface ToolInfo {
  name: string;
  description: string;
  path: string;
  available: boolean;
}

export interface ServiceInfo {
  toolDir: string;
  /** Where requests get their temporary job directories; nothing stays there. */
  scratchDir: string;
  tools: ToolInfo[];
}

/** A minidump kept in this browser; see minidumpStore.ts. */
export interface Minidump {
  id: string;
  name: string;
  size: number;
  /** Milliseconds since the epoch. */
  created: number;
  /** How it got here: opened from a file, or captured with pid2md / core2md. */
  source: 'file' | 'pid2md' | 'core2md';
  product?: string;
  version?: string;
  pid?: number;
}

/** A Breakpad symbol file added to a dump; see symbolFile.ts. */
export interface SymbolFile {
  /** `<name>/<id>`: one file per module build. */
  key: string;
  /** Module name from the MODULE line, e.g. libfoo.so or foo.pdb. */
  name: string;
  /** Debug id from the MODULE line. */
  id: string;
  os: string;
  arch: string;
  size: number;
  /** Milliseconds since the epoch. */
  added: number;
}

export interface StackwalkOptions {
  format: 'human' | 'machine';
  stack: boolean;
  crashOnly: boolean;
  brief: boolean;
  dumpPointers: boolean;
  thread: number | null;
}

/** dump_syms switches for turning an ELF file into a Breakpad symbol file. */
export interface DumpSymsOptions {
  /** Call frame info for unwinding; off passes -c. */
  cfi: boolean;
  /** -d: INLINE / INLINE_ORIGIN records. */
  inline: boolean;
  /** -i: only the MODULE / INFO header. */
  headerOnly: boolean;
  /** -r: skip references between compilation units. */
  noCrossCu: boolean;
  /** -a: keep the load address instead of normalizing it to zero. */
  preserveLoadAddress: boolean;
  /** -m: mark FUNC / PUBLIC records that cover several symbols. */
  multiple: boolean;
  /** -v: print every warning to stderr. */
  verbose: boolean;
  /** -b: module id to use instead of the build id. */
  id: string;
  /** -n: module name to use instead of the file name. */
  name: string;
  /** -o: operating system name to write instead of Linux. */
  os: string;
}
