import { withBase } from './base';
import type {
  DumpSymsOptions,
  ServiceInfo,
  StackwalkOptions,
  SymbolFile,
  ToolResult,
} from './types';

/** An API error; `result` is set when a tool ran and failed. */
export class ApiError extends Error {
  constructor(
    message: string,
    readonly status: number,
    readonly result?: ToolResult,
  ) {
    super(message);
  }
}

async function errorFrom(response: Response): Promise<ApiError> {
  let message = `Request failed with status ${response.status}`;
  let result: ToolResult | undefined;
  try {
    const body = (await response.json()) as { error?: string; result?: ToolResult };
    if (body?.error) message = body.error;
    result = body?.result;
  } catch {
    // No JSON body; keep the status-based message.
  }
  return new ApiError(message, response.status, result);
}

/** `path` is app-relative (`/dump_syms`); the base URL prefix is added here. */
async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(withBase(path), init);
  if (!response.ok) throw await errorFrom(response);
  return (await response.json()) as T;
}

/**
 * POSTs a form with upload progress, which fetch() cannot report.
 * `onProgress` receives a fraction between 0 and 1.
 */
function postForm<T>(path: string, form: FormData, onProgress?: (fraction: number) => void): Promise<T> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', withBase(path));
    xhr.responseType = 'text';
    xhr.upload.onprogress = (event) => {
      if (event.lengthComputable) onProgress?.(event.loaded / event.total);
    };
    xhr.onerror = () => reject(new ApiError('Network error during upload', 0));
    xhr.onload = () => {
      let body: { error?: string; result?: ToolResult } | undefined;
      try {
        body = JSON.parse(xhr.responseText);
      } catch {
        body = undefined;
      }
      if (xhr.status >= 200 && xhr.status < 300 && body) {
        resolve(body as T);
      } else {
        reject(new ApiError(body?.error ?? `Upload failed with status ${xhr.status}`, xhr.status, body?.result));
      }
    };
    xhr.send(form);
  });
}


export const fetchServiceInfo = (signal?: AbortSignal) => request<ServiceInfo>('/api/tools', { signal });

// ---- minidumps --------------------------------------------------------------
//
// The server keeps no minidumps: every call sends the dump along, and the
// server deletes its copy when the call ends.

/** A symbol file to send along with a stack walk. */
export interface SymbolAttachment {
  symbol: SymbolFile;
  blob: Blob;
}

/** A dump, its options and the symbol files for it as the form the minidump endpoints take. */
function dumpForm(dump: Blob, name: string, options: object, symbols: SymbolAttachment[] = []): FormData {
  const form = new FormData();
  form.append('options', JSON.stringify(options));
  form.append('minidump', dump, name);
  for (const { symbol, blob } of symbols) form.append('symbols', blob, `${symbol.name}.sym`);
  return form;
}

/** A download answered by the server: the file and the name it suggests. */
async function fileFrom(response: Response, fallbackName: string): Promise<{ blob: Blob; filename: string }> {
  if (!response.ok) throw await errorFrom(response);
  const disposition = response.headers.get('Content-Disposition') ?? '';
  const filename = /filename="([^"]+)"/.exec(disposition)?.[1] ?? fallbackName;
  return { blob: await response.blob(), filename };
}

/**
 * Walks the stack with the given symbol files: the dump and its symbols travel
 * in one request. `onProgress` gets the upload progress, 0..1.
 */
export const runStackwalk = (
  dump: Blob,
  name: string,
  options: StackwalkOptions,
  symbols: SymbolAttachment[],
  onProgress?: (fraction: number) => void,
) => postForm<{ result: ToolResult }>('/minidump_stackwalk', dumpForm(dump, name, options, symbols), onProgress);

// ---- symbols ----------------------------------------------------------------
//
// Symbol files live in the browser with their dumps (dumpSymbolStore.ts); the
// server only turns binaries into them, keeping nothing.

/**
 * Runs dump_syms on an ELF file and resolves with the symbol file it wrote;
 * the server keeps neither. `debug` is the separate debug file the binary's
 * .gnu_debuglink names, when its debug info was split off. `onProgress` gets
 * the upload progress, 0..1.
 */
export function dumpSymbols(
  binary: File,
  debug: File | null,
  options: DumpSymsOptions,
  onProgress?: (fraction: number) => void,
): Promise<{ blob: Blob; filename: string }> {
  const form = new FormData();
  form.append('options', JSON.stringify(options));
  form.append('binary', binary, binary.name);
  if (debug) form.append('debug', debug, debug.name);
  return postFormForFile('/dump_syms', form, `${binary.name}.sym`, 'dump_syms', onProgress);
}

// ---- microdumps and capture ---------------------------------------------------

/** Walks a microdump's stack; like minidumps, its symbol files travel in the same request. */
export function runMicrodumpStackwalk(
  input: { text: string } | { file: File },
  options: { machine: boolean; stack: boolean },
  symbols: SymbolAttachment[],
): Promise<{ result: ToolResult }> {
  const form = new FormData();
  form.append('machine', options.machine ? '1' : '0');
  form.append('stack', options.stack ? '1' : '0');
  if ('file' in input) form.append('microdump', input.file, input.file.name);
  else form.append('microdump', input.text);
  for (const { symbol, blob } of symbols) form.append('symbols', blob, `${symbol.name}.sym`);
  return postForm('/microdump_stackwalk', form);
}

/** Runs pid2md; resolves with the minidump, which the server does not keep. */
export async function captureProcess(pid: number): Promise<{ blob: Blob; filename: string }> {
  const response = await fetch(withBase('/api/capture/pid2md'), {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ pid }),
  });
  return fileFrom(response, `pid-${pid}.dmp`);
}

/** The proc files core2md reads from the crashed process's /proc/<pid>. */
export const PROC_FILES = ['auxv', 'cmdline', 'environ', 'maps', 'status'] as const;
export type ProcFile = (typeof PROC_FILES)[number];

/** Runs core2md; resolves with the minidump, which the server does not keep. */
export function convertCoreToMinidump(
  core: File,
  procFiles: Partial<Record<ProcFile, File>>,
  onProgress?: (fraction: number) => void,
): Promise<{ blob: Blob; filename: string }> {
  const form = new FormData();
  for (const name of PROC_FILES) {
    const file = procFiles[name];
    if (file) form.append(name, file, file.name);
  }
  form.append('core', core, core.name);
  return postFormForFile('/api/capture/core2md', form, `${core.name}.dmp`, 'core2md', onProgress);
}

/**
 * POSTs a form whose answer is a file download, with upload progress, which
 * fetch() cannot report. A failure answers JSON instead, naming `tool` in
 * the message when it has none of its own.
 */
function postFormForFile(
  path: string,
  form: FormData,
  fallbackName: string,
  tool: string,
  onProgress?: (fraction: number) => void,
): Promise<{ blob: Blob; filename: string }> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', withBase(path));
    xhr.responseType = 'blob';
    xhr.upload.onprogress = (event) => {
      if (event.lengthComputable) onProgress?.(event.loaded / event.total);
    };
    xhr.onerror = () => reject(new ApiError('Network error during upload', 0));
    xhr.onload = async () => {
      const blob = xhr.response as Blob;
      if (xhr.status >= 200 && xhr.status < 300) {
        const disposition = xhr.getResponseHeader('Content-Disposition') ?? '';
        resolve({ blob, filename: /filename="([^"]+)"/.exec(disposition)?.[1] ?? fallbackName });
        return;
      }
      let body: { error?: string; result?: ToolResult } | undefined;
      try {
        body = JSON.parse(await blob.text());
      } catch {
        body = undefined;
      }
      reject(new ApiError(body?.error ?? `${tool} failed with status ${xhr.status}`, xhr.status, body?.result));
    };
    xhr.send(form);
  });
}
