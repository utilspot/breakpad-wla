import { useEffect, useState } from 'react';
import { fetchServiceInfo } from '../api';
import { errorMessage } from '../format';
import type { ServiceInfo } from '../types';

export default function ToolsPage() {
  const [info, setInfo] = useState<ServiceInfo | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const controller = new AbortController();
    fetchServiceInfo(controller.signal)
      .then(setInfo)
      .catch((cause) => {
        if (!controller.signal.aborted) setError(errorMessage(cause));
      });
    return () => controller.abort();
  }, []);

  if (error) return <p className="error">{error}</p>;
  if (!info) return <p className="muted">Loading…</p>;

  const missing = info.tools.filter((tool) => !tool.available);

  return (
    <div className="page-body">
      <section>
        <h2>Tools</h2>
        {missing.length ? (
          <p className="notice">
            {missing.length} tool{missing.length === 1 ? ' is' : 's are'} missing from <code>{info.toolDir}</code>; the pages
            using {missing.length === 1 ? 'it' : 'them'} will report an error.
          </p>
        ) : null}
        <table className="data">
          <thead>
            <tr>
              <th>Tool</th>
              <th>What it does here</th>
              <th>Status</th>
            </tr>
          </thead>
          <tbody>
            {info.tools.map((tool) => (
              <tr key={tool.name}>
                <td className="mono" title={tool.path}>
                  {tool.name}
                </td>
                <td>{tool.description}</td>
                <td>
                  <span className={`badge ${tool.available ? 'badge-ok' : 'badge-error'}`}>
                    {tool.available ? 'available' : 'missing'}
                  </span>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </section>

      <section>
        <h2>Where files are kept</h2>
        <dl className="facts">
          <div>
            <dt>Minidumps and symbol files</dt>
            <dd>In each user's browser (IndexedDB). The server keeps none.</dd>
          </div>
          <div>
            <dt>Server scratch space</dt>
            <dd className="mono">{info.scratchDir}</dd>
          </div>
          <div>
            <dt>Tools</dt>
            <dd className="mono">{info.toolDir}</dd>
          </div>
        </dl>
        <p className="muted">
          A stack walk sends the minidump together with the symbol files its modules need, in one request. The server lays
          them out in a scratch directory for the tool and deletes it when the request ends. So crash reporters and{' '}
          <code>sym_upload</code> cannot send files to this server; add them on the Minidumps and Symbols pages instead.
        </p>
      </section>
    </div>
  );
}
