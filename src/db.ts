/**
 * The browser database holding the user's files. The server keeps none of
 * them: minidumps and symbol files live here and are sent along with each
 * request that needs them.
 */

const DB_NAME = 'breakpad';
const DB_VERSION = 4;

export const MINIDUMPS = 'minidumps';
export const SYMBOLS = 'symbols';
/** Symbol files attached to one minidump, keyed `<minidump id>/<module name>/<debug id>`. */
export const DUMP_SYMBOLS = 'dumpSymbols';
/** The last stack walk of each minidump, keyed by minidump id. */
export const STACKWALKS = 'stackwalks';

let dbPromise: Promise<IDBDatabase> | null = null;

function openDb(): Promise<IDBDatabase> {
  dbPromise ??= new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, DB_VERSION);
    request.onupgradeneeded = () => {
      const db = request.result;
      // Version 1 had only minidumps, version 2 no per-dump symbols, version 3 no
      // stack walk cache; each store is created when missing.
      if (!db.objectStoreNames.contains(MINIDUMPS)) db.createObjectStore(MINIDUMPS, { keyPath: 'id' });
      if (!db.objectStoreNames.contains(SYMBOLS)) db.createObjectStore(SYMBOLS, { keyPath: 'key' });
      if (!db.objectStoreNames.contains(DUMP_SYMBOLS)) db.createObjectStore(DUMP_SYMBOLS, { keyPath: 'key' });
      if (!db.objectStoreNames.contains(STACKWALKS)) db.createObjectStore(STACKWALKS, { keyPath: 'minidumpId' });
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => {
      dbPromise = null;
      reject(request.error ?? new Error('Cannot open browser storage'));
    };
  });
  return dbPromise;
}

/** Runs one request in its own transaction and resolves once the transaction commits. */
export async function run<T>(
  storeName: string,
  mode: IDBTransactionMode,
  action: (store: IDBObjectStore) => IDBRequest<T>,
): Promise<T> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const transaction = db.transaction(storeName, mode);
    const request = action(transaction.objectStore(storeName));
    transaction.oncomplete = () => resolve(request.result);
    transaction.onerror = () => reject(transaction.error ?? request.error);
    transaction.onabort = () => reject(transaction.error ?? new Error('Browser storage transaction aborted'));
  });
}

/** Asks the browser not to evict the stored files under storage pressure; best effort. */
export function requestPersistence(): void {
  navigator.storage?.persist?.().catch(() => undefined);
}
