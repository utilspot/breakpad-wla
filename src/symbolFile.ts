import { dumpSymbols, type SymbolAttachment } from './api';
import type { SymbolFile } from './types';

/**
 * Breakpad symbol files as the user adds them. They are kept with the dump
 * they belong to (see dumpSymbolStore.ts) and sent along with its stack walks;
 * the server keeps none.
 */

/** `<module name>/<debug id>`: one file per module build. */
export const symbolKey = (name: string, id: string) => `${name}/${id}`;

/**
 * Parses the `MODULE <os> <arch> <id> <name>` header; null when the blob is not a symbol file.
 * The name ends at the first space: in `MODULE Linux x86_64 <id> app 1.0.0` it is `app`.
 */
export async function readSymbolModule(blob: Blob): Promise<Pick<SymbolFile, 'os' | 'arch' | 'id' | 'name'> | null> {
  const head = await blob.slice(0, 1024).text();
  const line = head.split(/\r?\n/, 1)[0];
  const match = /^MODULE (\S+) (\S+) ([0-9A-Fa-f]+) (\S+)/.exec(line);
  if (!match) return null;
  const [, os, arch, id, name] = match;
  return { os, arch, id, name };
}

/** A symbol file with its MODULE fields, ready to keep and send. */
export async function toAttachment(blob: Blob): Promise<SymbolAttachment> {
  const module = await readSymbolModule(blob);
  if (!module) throw new Error("Not a Breakpad symbol file: it must start with 'MODULE <os> <arch> <id> <name>'");
  const symbol: SymbolFile = { key: symbolKey(module.name, module.id), ...module, size: blob.size, added: Date.now() };
  // Copy File objects into plain blobs so the result does not depend on the picked file.
  return { symbol, blob: new Blob([blob], { type: 'text/plain' }) };
}

/** True if the file starts with the ELF magic, 0x7f "ELF". */
async function isElf(file: Blob): Promise<boolean> {
  const head = new Uint8Array(await file.slice(0, 4).arrayBuffer());
  return head.length === 4 && head[0] === 0x7f && head[1] === 0x45 && head[2] === 0x4c && head[3] === 0x46;
}

/**
 * A Breakpad symbol file from a file the user picked: a .sym file as it is, an
 * ELF file through dump_syms on the server. Throws an ApiError with the
 * dump_syms output when that fails.
 */
export async function toSymbolFile(file: File, dumpSymsFlags: { cfi: boolean; inline: boolean }): Promise<Blob> {
  if (await readSymbolModule(file)) return file;
  if (!(await isElf(file))) throw new Error('Neither a Breakpad .sym file nor an ELF file');
  const { blob } = await dumpSymbols(file, null, {
    ...dumpSymsFlags,
    headerOnly: false,
    noCrossCu: false,
    preserveLoadAddress: false,
    multiple: false,
    verbose: false,
    id: '',
    name: '',
    os: '',
  });
  return blob;
}
