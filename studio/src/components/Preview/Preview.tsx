import { Component, useState } from 'react';
import type { ReactNode } from 'react';
import { Icon } from '../Icon';
import { Renderer } from './Renderer';
import type { StudioState } from '../../state/studioState';
class RenderBoundary extends Component<{children:ReactNode}, {failed:boolean}> {
  state = {failed:false};
  static getDerivedStateFromError() { return {failed:true}; }
  render() { return this.state.failed ? <p role="alert">Visualization unavailable. Check WebGL support, then retry Preview.</p> : this.props.children; }
}
export function Preview({ state, onField, onPoint, onGenerate, previousConfig=false }: { previousConfig?:boolean; onGenerate:()=>void; state: StudioState; onField: (field: StudioState['field']) => void; onPoint: (x:number,y:number)=>void }) {
  const [view, setView] = useState(0);
  const data = state.data?.[state.field];
  const status=previousConfig ? 'Previous preview — not generated from current config' : state.preview==='generating' ? 'Generating mock data…' : state.preview==='failed' ? state.error ?? 'Preview failed' : 'Stale — showing previous preview';
  return <section className="preview-panel" aria-labelledby="preview-heading">
    <div className="preview-heading"><div><span className="eyebrow">MOCK WORKSPACE</span><h1 id="preview-heading">Initial condition preview</h1></div><div className="preview-context"><span className="quiet-tag" role="status">Preview: {state.preview}</span></div></div>
    <div className="viewport-toolbar"><div className="field-placeholder"><Icon name="grid" /><select aria-label="Preview field" value={state.field} onChange={e => onField(e.target.value as StudioState['field'])}><option value="density">Density</option><option value="temperature">Temperature</option><option value="pressure">Pressure</option></select><strong title={state.field==='pressure' ? 'Mock pressure uses an illustrative step field to exercise field switching.' : 'Dimensionless illustrative data.'}>{state.field==='pressure' ? 'Mock · step field' : 'Mock'}</strong></div><div className="viewport-options"><span>Viridis</span><button disabled={!data} aria-label="Fit preview" title="Reset zoom and pan" onClick={() => setView(v => v + 1)}><Icon name="expand" /></button></div></div>
    <div className="viewport mock-viewport" aria-busy={state.preview === 'generating'}>
      {data ? <RenderBoundary key={view}><Renderer data={data} selected={state.selected} onPoint={onPoint} enabled={!previousConfig && state.preview === 'current'} status={previousConfig || state.preview!=='current' ? status : undefined} /></RenderBoundary> : <div className="canvas-status-layer"><div className="empty-preview"><div className="empty-icon"><Icon name="layers" size={30} /></div><h2>{state.preview==='generating' ? 'Generating preview…' : state.preview==='failed' ? 'Preview failed' : 'No preview generated'}</h2><p>{previousConfig ? 'No preview has been generated in this session. Switch to Mock Demo to generate one.' : state.preview==='failed' ? state.error : 'Edit parameters, then generate a preview.'}</p>{!previousConfig && <button disabled={state.config==='invalid' || state.preview==='generating'} onClick={onGenerate}>Generate Preview</button>}</div></div>}

    </div>
    <div className="preview-footer"><span>{data ? `${data.width} × ${data.height} · Float32` : 'No data'} · Mock / Demo</span><span>Range <strong>{data ? `${data.min.toPrecision(4)} – ${data.max.toPrecision(4)}` : '—'}</strong></span></div>
    <div className="demo-notice"><span className="notice-icon">i</span><p><strong>Mock data — not an ARCH scientific result.</strong> Dimensionless illustration. Drag to pan; scroll to zoom. Switch fields to compare mock patterns. Click a cell to inspect its mock values.</p></div>
  </section>;
}
