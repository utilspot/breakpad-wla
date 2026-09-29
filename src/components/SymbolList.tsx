import { useRef, useState, type DragEvent } from 'react';
import { ApiError, type SymbolAttachment } from '../api';
import { errorMessage, formatBytes, saveBlob } from '../format';
import { toSymbolFile } from '../symbolFile';
import type { ToolResult } from '../types';
import ToolOutput from './ToolOutput';

/** A symbol file the user picked that could not be added. */
interface AddFailure {
  file: string;
  error: string;
  result?: ToolResult;
}

export interface SymbolAdder {
  adding: boolean;
  failures: AddFailure[];
  add: (files: File[]) => Promise<void>;
}

/**
 * Adds picked or dropped files as symbol files: .sym files as they are, ELF
 * files through dump_syms on the server first. `onAdd` takes one Breakpad
 * symbol file and throws when it cannot.
 */
export function useSymbolAdder(onAdd: (blob: Blob) => Promise<unknown>): SymbolAdder {
  const [adding, setAdding] = useState(false);
  const [failures, setFailures] = useState<AddFailure[]>([]);

  const add = async (files: File[]) => {
    setAdding(true);
    const failed: AddFailure[] = [];
    for (const file of files) {
      try {
        await onAdd(await toSymbolFile(file, { cfi: true, inline: false }));
      } catch (cause) {
        failed.push({ file: file.name, error: errorMessage(cause), result: cause instanceof ApiError ? cause.result : undefined });
      }
    }
    setFailures(failed);
    setAdding(false);
  };

  return { adding, failures, add };
}

/**
 * Makes an element take dropped files. Only drags that carry files count, and
 * a counter keeps `dragging` steady while the pointer crosses child elements.
 */
export function useFileDrop(onFiles: (files: File[]) => void) {
  const depth = useRef(0);
  const [dragging, setDragging] = useState(false);
  const hasFiles = (event: DragEvent) => event.dataTransfer.types.includes('Files');

  const dropProps = {
    onDragEnter: (event: DragEvent) => {
      if (!hasFiles(event)) return;
      event.preventDefault();
      depth.current += 1;
      setDragging(true);
    },
    onDragOver: (event: DragEvent) => {
      if (hasFiles(event)) event.preventDefault();
    },
    onDragLeave: (event: DragEvent) => {
      if (!hasFiles(event)) return;
      depth.current = Math.max(0, depth.current - 1);
      if (depth.current === 0) setDragging(false);
    },
    onDrop: (event: DragEvent) => {
      if (!hasFiles(event)) return;
      event.preventDefault();
      depth.current = 0;
      setDragging(false);
      const files = [...event.dataTransfer.files];
      if (files.length) onFiles(files);
    },
  };

  return { dragging, dropProps };
}

interface SymbolListProps {
  /** null while loading. */
  symbols: SymbolAttachment[] | null;
  error?: string | null;
  emptyText: string;
  /** Adds one Breakpad symbol file; throws when it cannot. */
  onAdd: (blob: Blob) => Promise<unknown>;
  onRemove: (key: string) => void;
  /**
   * Set when a surrounding element takes the dropped files (see useFileDrop);
   * the list then shares its adder instead of taking drops itself.
   */
  adder?: SymbolAdder;
}

/** The symbol files of one dump: added or dropped here, all sent with every stack walk of it. */
export default function SymbolList({ symbols, error, emptyText, onAdd, onRemove, adder }: SymbolListProps) {
  const input = useRef<HTMLInputElement>(null);
  const own = useSymbolAdder(onAdd);
  const { adding, failures, add } = adder ?? own;
  const { dragging, dropProps } = useFileDrop(add);

  return (
    <section className={`dump-symbols${!adder && dragging ? ' drop-active' : ''}`} {...(adder ? {} : dropProps)}>
      <div className="list-header">
        <h3>Symbols</h3>
        <span className="muted">
          {symbols?.length ? `${symbols.length} file${symbols.length === 1 ? '' : 's'}, sent with every stack walk` : ''}
        </span>
        <div className="spacer" />
        <button type="button" className="button-small" disabled={adding} onClick={() => input.current?.click()}>
          {adding ? 'Adding…' : 'Add symbols'}
        </button>
        <input
          ref={input}
          type="file"
          multiple
          hidden
          onChange={(event) => {
            const files = [...(event.target.files ?? [])];
            // Reset so choosing the same file again still triggers a change.
            event.target.value = '';
            if (files.length) add(files);
          }}
        />
      </div>
      {error ? <p className="error">{error}</p> : null}
      {failures.map((failure, index) => (
        <div key={`${failure.file}-${index}`} className="upload-outcome">
          <p className="error">
            {failure.file}: {failure.error}
          </p>
          {failure.result ? (
            <details>
              <summary>dump_syms output</summary>
              <ToolOutput result={failure.result} saveAs={`${failure.file}.dump_syms`} />
            </details>
          ) : null}
        </div>
      ))}
      {symbols?.length === 0 ? (
        <p className="muted">{emptyText} Add or drop Breakpad .sym files, or ELF files to run through dump_syms.</p>
      ) : null}
      {symbols?.length ? (
        <table className="data">
          <thead>
            <tr>
              <th>Module</th>
              <th>Debug id</th>
              <th>OS</th>
              <th>Arch</th>
              <th className="num">Size</th>
              <th />
            </tr>
          </thead>
          <tbody>
            {symbols.map(({ symbol, blob }) => (
              <tr key={symbol.key}>
                <td className="mono">{symbol.name}</td>
                <td className="mono muted">{symbol.id}</td>
                <td>{symbol.os}</td>
                <td>{symbol.arch}</td>
                <td className="num">{formatBytes(symbol.size)}</td>
                <td className="row-actions">
                  <button type="button" className="button-small" onClick={() => saveBlob(blob, `${symbol.name}.sym`)}>
                    Download
                  </button>
                  <button type="button" className="button-small danger" onClick={() => onRemove(symbol.key)}>
                    Remove
                  </button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      ) : null}
    </section>
  );
}
