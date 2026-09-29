interface UploadProgressLineProps {
  /** Upload progress, 0..1; at 1 the bar slides while the server works. */
  upload: number;
  /** Shown while uploading, before the percentage, e.g. `Uploading the minidump`. */
  uploading: string;
  /** Shown once the upload is done, e.g. `Walking the stack…`. */
  working: string;
}

/** The upload of a tool run's files, then a sliding bar while the server runs the tool. */
export default function UploadProgressLine({ upload, uploading, working }: UploadProgressLineProps) {
  return (
    <div className="upload-progress" role="status">
      <span className="muted">{upload < 1 ? `${uploading}… ${Math.round(upload * 100)}%` : working}</span>
      <div className={`progress${upload >= 1 ? ' indeterminate' : ''}`} aria-hidden>
        <div style={{ width: `${Math.round(upload * 100)}%` }} />
      </div>
    </div>
  );
}
