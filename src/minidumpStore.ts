import { MINIDUMPS, requestPersistence, run } from './db';
import { deleteDumpSymbols } from './dumpSymbolStore';
import { deleteCachedStackwalk } from './stackwalkCache';
import type { Minidump } from './types';

/**
 * Minidumps are kept in this browser (IndexedDB, see db.ts), never on the
 * server: the server only gets a copy for the duration of each tool run. They
 * are per browser and per site, and go away when the site's data is cleared.
 */

/** Fired on window whenever a minidump is added or deleted. */
export const MINIDUMPS_CHANGED = 'breakpad:minidumps-changed';

interface Record extends Minidump {
  blob: Blob;
}

const notify = () => window.dispatchEvent(new Event(MINIDUMPS_CHANGED));

/** Not crypto.randomUUID(): that only exists on https and localhost. */
const newId = () => `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`;

/** True if the blob starts with the minidump signature "MDMP". */
export async function isMinidump(blob: Blob): Promise<boolean> {
  const head = new Uint8Array(await blob.slice(0, 4).arrayBuffer());
  return head.length === 4 && String.fromCharCode(...head) === 'MDMP';
}

export async function listMinidumps(): Promise<Minidump[]> {
  const records = await run<Record[]>(MINIDUMPS, 'readonly', (store) => store.getAll());
  return records
    .map(({ blob: _blob, ...minidump }) => minidump)
    .sort((a, b) => b.created - a.created);
}

export async function getMinidumpBlob(id: string): Promise<Blob | null> {
  const record = await run<Record | undefined>(MINIDUMPS, 'readonly', (store) => store.get(id));
  return record?.blob ?? null;
}

export async function addMinidump(
  blob: Blob,
  details: Pick<Minidump, 'name' | 'source'> & Partial<Pick<Minidump, 'product' | 'version' | 'pid'>>,
): Promise<Minidump> {
  if (!(await isMinidump(blob))) throw new Error('Not a minidump: the file does not start with "MDMP"');

  const minidump: Minidump = { id: newId(), size: blob.size, created: Date.now(), ...details };
  // Copy File objects into plain blobs so the record does not depend on the picked file.
  await run(MINIDUMPS, 'readwrite', (store) => store.add({ ...minidump, blob: new Blob([blob]) }));
  requestPersistence();
  notify();
  return minidump;
}

export async function deleteMinidump(id: string): Promise<void> {
  await run(MINIDUMPS, 'readwrite', (store) => store.delete(id));
  await deleteDumpSymbols(id);
  await deleteCachedStackwalk(id);
  notify();
}
