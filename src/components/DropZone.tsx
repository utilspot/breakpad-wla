import { useId, useRef, useState, type DragEvent, type ReactNode } from 'react';

interface DropZoneProps {
  label: ReactNode;
  hint?: ReactNode;
  accept?: string;
  multiple?: boolean;
  disabled?: boolean;
  /** 0..1 while an upload is running. */
  progress?: number | null;
  onFiles: (files: File[]) => void;
}

/** A file picker that also takes files dropped onto it. */
export default function DropZone({ label, hint, accept, multiple, disabled, progress, onFiles }: DropZoneProps) {
  const inputId = useId();
  const input = useRef<HTMLInputElement>(null);
  const [dragging, setDragging] = useState(false);

  const onDrop = (event: DragEvent) => {
    event.preventDefault();
    setDragging(false);
    if (disabled) return;
    const files = [...event.dataTransfer.files];
    if (files.length) onFiles(multiple ? files : files.slice(0, 1));
  };

  const busy = progress !== null && progress !== undefined;

  return (
    <div
      className={`dropzone${dragging ? ' dragging' : ''}${disabled ? ' disabled' : ''}`}
      onDragOver={(event) => {
        event.preventDefault();
        if (!disabled) setDragging(true);
      }}
      onDragLeave={() => setDragging(false)}
      onDrop={onDrop}
    >
      <label htmlFor={inputId}>
        <strong>{label}</strong>
        {hint ? <span className="muted">{hint}</span> : null}
        <span className="dropzone-action">{busy ? `Uploading… ${Math.round(progress * 100)}%` : 'Choose file or drop it here'}</span>
      </label>
      <input
        ref={input}
        id={inputId}
        type="file"
        accept={accept}
        multiple={multiple}
        disabled={disabled}
        onChange={(event) => {
          const files = [...(event.target.files ?? [])];
          // Reset so choosing the same file again still triggers a change.
          event.target.value = '';
          if (files.length) onFiles(files);
        }}
      />
      {busy ? (
        <div className="progress" aria-hidden>
          <div style={{ width: `${Math.round(progress * 100)}%` }} />
        </div>
      ) : null}
    </div>
  );
}
