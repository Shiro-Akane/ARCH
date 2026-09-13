import type { PlotfileMetadata } from '../../data/PlotfilePreviewProvider';
import type { LinePreviewData } from '../../data/LinePreviewData';
export function PlotfileInspector({ info, line, selected, onSelect }: { info: PlotfileMetadata; line: LinePreviewData | null; selected: number | null; onSelect: (index: number) => void }) {
  return <aside className="plotfile-inspector" aria-label="Real Plotfile Inspector"><h2>Inspector</h2><span className="demo-badge">REAL PLOTFILE</span>
    <dl><dt>File</dt><dd>{info.file}</dd><dt>Time</dt><dd>{info.time}</dd><dt>Dimension</dt><dd>{info.dimension}D</dd><dt>Geometry</dt><dd>{info.geometry}</dd>
    <dt>Field</dt><dd>{line?.field ?? 'Unavailable — select a supported field'}</dd><dt>Min</dt><dd>{line?.min ?? '—'}</dd><dt>Max</dt><dd>{line?.max ?? '—'}</dd>
    <dt>x</dt><dd>{line && selected !== null ? line.x[selected] : '—'}</dd><dt>Value</dt><dd>{line && selected !== null ? line.values[selected] : '—'}</dd></dl>
    {line && <><p>Click the curve area to inspect the nearest sample, or choose a sample below.</p><label>Sample (1–{line.values.length}) <input aria-label="Sample number" type="number" min="1" max={line.values.length} value={selected === null ? '' : selected + 1} onChange={e => { const n = Number(e.target.value); if (Number.isInteger(n) && n >= 1 && n <= line.values.length) onSelect(n - 1); }} /></label></>}
  </aside>;
}
