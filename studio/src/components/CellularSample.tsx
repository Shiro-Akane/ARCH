import { useState } from 'react';
import sample from '../samples/cellular.json';
const fields = { DENS: ['Density', 'g/cm³'], TEMP: ['Temperature', 'K'], PRES: ['Pressure', 'erg/cm³'] } as const;
export function CellularSample() {
  const [index, setIndex] = useState(1);
  const frame = sample.frames[index];
  return <main className="sample-page">
    <header><span className="demo-badge">REAL ARCH OUTPUT · READ ONLY</span><h1>CellularDet · 1D result</h1><p>CPU · Helmholtz EOS · aprox19 · {sample.cells} AMR cells / {sample.blocks} leaf blocks</p></header>
    <label className="snapshot-select">Snapshot <select value={index} onChange={e=>setIndex(Number(e.target.value))}>{sample.frames.map((f,i)=><option key={f.source} value={i}>{f.label} · t = {f.time.toExponential(6)} s</option>)}</select></label>
    <p className="sample-warning">Stopped at the 200-step limit. Final time {sample.frames[1].time.toExponential(6)} s; requested tmax = 5e-8 s was not reached. This is a 1D run with zero transverse perturbation, not a 2D cellular pattern.</p>
    <div className="sample-charts">{(Object.keys(fields) as (keyof typeof fields)[]).map(key=>{
      const values = frame.fields[key]; const min=Math.min(...values), max=Math.max(...values); const span=max-min || 1;
      const points=values.map((v,i)=>`${70+frame.x[i]/128*780},${190-(v-min)/span*155}`).join(' ');
      return <section key={key}><h2>{fields[key][0]} <small>{fields[key][1]}</small></h2><svg viewBox="0 0 900 235" role="img" aria-label={`${fields[key][0]} versus x for ${frame.label}`}>
        <path d="M70 25V190H850" fill="none" stroke="#728697"/><text x="65" y="35" textAnchor="end">{max.toExponential(2)}</text><text x="65" y="190" textAnchor="end">{min.toExponential(2)}</text>
        {[0,32,64,96,128].map(x=><g key={x}><path d={`M${70+x/128*780} 190v5`} stroke="#728697"/><text x={70+x/128*780} y="212" textAnchor="middle">{x}</text></g>)}
        <polyline points={points} fill="none" stroke="#ecb376" strokeWidth="1.5" vectorEffect="non-scaling-stroke"/><text x="460" y="230" textAnchor="middle">x (cm)</text>
      </svg></section>;
    })}</div>
    <p>Native nonuniform cell-center samples, sorted by x; lines connect samples without resampling. No smoothing or physical-model reconstruction.</p>
    <details><summary>Source and provenance</summary><p>{frame.source}</p><p>SHA-256: {frame.sha256}</p><p>Fields: Data/DENS, Data/TEMP, Data/PRES; coordinates: Grid/x. Original HDF5 files retained unchanged. Static sample imported offline.</p></details>
  </main>;
}
