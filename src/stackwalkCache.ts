import { STACKWALKS, run } from './db';
import type { StackwalkOptions, ToolResult } from './types';

/**
 * The last stack walk of each minidump, kept in this browser so opening the
 * dump shows it without asking the server. Only "Run again" replaces it.
 */
export interface CachedStackwalk {
  minidumpId: string;
  /** The options it ran with; `format` decides how the output is shown. */
  options: StackwalkOptions;
  result: ToolResult;
  /** Milliseconds since the epoch. */
  ran: number;
}

export async function getCachedStackwalk(minidumpId: string): Promise<CachedStackwalk | null> {
  return (await run<CachedStackwalk | undefined>(STACKWALKS, 'readonly', (store) => store.get(minidumpId))) ?? null;
}

export async function cacheStackwalk(entry: CachedStackwalk): Promise<void> {
  await run(STACKWALKS, 'readwrite', (store) => store.put(entry));
}

export async function deleteCachedStackwalk(minidumpId: string): Promise<void> {
  await run(STACKWALKS, 'readwrite', (store) => store.delete(minidumpId));
}
