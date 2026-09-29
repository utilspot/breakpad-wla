import { useEffect, useState } from 'react';
import type { SymbolAttachment } from './api';
import { DUMP_SYMBOLS, requestPersistence, run } from './db';
import { errorMessage } from './format';
import { toAttachment } from './symbolFile';
import type { SymbolFile } from './types';

/**
 * Symbol files attached to one minidump. Each dump has its own list, and every
 * stack walk of the dump sends all of them in the same request as the dump.
 * They go away with the dump.
 */

/** Fired on window whenever a minidump's symbol files change; `detail` is the minidump id. */
export const DUMP_SYMBOLS_CHANGED = 'breakpad:dump-symbols-changed';

interface Record extends SymbolAttachment {
  /** `<minidump id>/<symbol key>`: minidump ids have no '/', so a dump's files share the prefix. */
  key: string;
}

const notify = (minidumpId: string) => window.dispatchEvent(new CustomEvent(DUMP_SYMBOLS_CHANGED, { detail: minidumpId }));

/** Every key of one minidump's files. */
const dumpRange = (minidumpId: string) => IDBKeyRange.bound(`${minidumpId}/`, `${minidumpId}/￿`);

export async function listDumpSymbols(minidumpId: string): Promise<SymbolAttachment[]> {
  const records = await run<Record[]>(DUMP_SYMBOLS, 'readonly', (store) => store.getAll(dumpRange(minidumpId)));
  return records.map(({ symbol, blob }) => ({ symbol, blob })).sort((a, b) => a.symbol.name.localeCompare(b.symbol.name));
}

/** Attaches a symbol file to a minidump, replacing one for the same module and id. */
export async function addDumpSymbol(minidumpId: string, blob: Blob): Promise<SymbolFile> {
  const { symbol, blob: copy } = await toAttachment(blob);
  const record: Record = { key: `${minidumpId}/${symbol.key}`, symbol, blob: copy };
  await run(DUMP_SYMBOLS, 'readwrite', (store) => store.put(record));
  requestPersistence();
  notify(minidumpId);
  return symbol;
}

export async function removeDumpSymbol(minidumpId: string, key: string): Promise<void> {
  await run(DUMP_SYMBOLS, 'readwrite', (store) => store.delete(`${minidumpId}/${key}`));
  notify(minidumpId);
}

export async function deleteDumpSymbols(minidumpId: string): Promise<void> {
  await run(DUMP_SYMBOLS, 'readwrite', (store) => store.delete(dumpRange(minidumpId)));
  notify(minidumpId);
}

/** A minidump's symbol files, reloaded whenever they change. null while loading. */
export function useDumpSymbols(minidumpId: string): { symbols: SymbolAttachment[] | null; error: string | null } {
  const [symbols, setSymbols] = useState<SymbolAttachment[] | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    const load = () => {
      listDumpSymbols(minidumpId)
        .then((list) => {
          if (cancelled) return;
          setSymbols(list);
          setError(null);
        })
        .catch((cause) => {
          if (cancelled) return;
          setSymbols([]);
          setError(`Browser storage is not available: ${errorMessage(cause)}`);
        });
    };
    const onChange = (event: Event) => {
      if ((event as CustomEvent<string>).detail === minidumpId) load();
    };
    load();
    window.addEventListener(DUMP_SYMBOLS_CHANGED, onChange);
    return () => {
      cancelled = true;
      window.removeEventListener(DUMP_SYMBOLS_CHANGED, onChange);
    };
  }, [minidumpId]);

  return { symbols, error };
}
