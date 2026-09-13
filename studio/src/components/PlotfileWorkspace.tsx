import { useRef, useState } from 'react';
import { inspectPlotfile, readPlotfileField } from '../data/PlotfilePreviewProvider';
import type { PlotfileMetadata } from '../data/PlotfilePreviewProvider';
import type { LinePreviewData } from '../data/LinePreviewData';
import { createLatestRequest, selectedFile } from '../data/latestRequest';
import { nearestSample } from '../data/LinePreviewData';
import { PlotfileInspector } from './Inspector/PlotfileInspector';
import { LineRenderer } from './Preview/LineRenderer';

export function PlotfileWorkspace() {
  const [message, setMessage] = useState('Choose a local, fully written ARCH plotfile (up to 16 MiB).');
  const [file, setFile] = useState<File | null>(null);
  const [info, setInfo] = useState<PlotfileMetadata | null>(null);
  const [field, setField] = useState('');
  const [line, setLine] = useState<LinePreviewData | null>(null);
  const [selected, setSelected] = useState<number | null>(null);
  const latest = useRef(createLatestRequest());
  async function selectField(next: string) {
    if (!file) return;
    setField(next); setLine(null); setSelected(null); setMessage('Reading field…');
    await latest.current(() => readPlotfileField(file, next), result => {
      setLine(result); setMessage('Ready · read only');
    }, error => setMessage(error instanceof Error ? error.message : 'Could not read field.'));
  }
  return <main className="sample-page plotfile-page" id="plotfile-workspace">
    <h1>Real Plotfile</h1>
    <label className="open-plotfile">Open Plotfile…<input aria-label="Open Plotfile" type="file" accept=".h5,.hdf5" onChange={async e => {
      const chosen = selectedFile(e.target.files); e.target.value = ''; if (!chosen) return;
        setFile(null); setInfo(null); setLine(null); setSelected(null); setField(''); setMessage('Opening…');
      await latest.current(() => inspectPlotfile(chosen), metadata => {
        setFile(chosen); setInfo(metadata); setMessage('Select a field.');
      }, error => setMessage(error instanceof Error ? error.message : 'Could not read file.'));
    }} /></label>
    <p role="status">{message}</p>
    {info && <><p>{info.file} · time {info.time} · {info.dimension}D · {info.geometry}</p>
      <label>Field <select aria-label="Plotfile field" value={field} onChange={e => void selectField(e.target.value)}><option value="" disabled>Select field</option>{info.fields.map(name => <option key={name} value={name}>{name}</option>)}</select></label>
    </>}
    <div className="plotfile-content">{line && <section aria-label="Real 1D preview"><LineRenderer data={line} selected={selected} onPoint={x => setSelected(nearestSample(line, x))} /><p>{line.values.length} samples · x coordinate and field units as stored by ARCH</p></section>}
      {info && <PlotfileInspector info={info} line={line} selected={selected} onSelect={setSelected} />}</div>
    <nav aria-label="Unavailable execution actions"><button disabled>Build</button><button disabled>Start</button><button disabled>Monitor</button></nav>
  </main>;
}
