import type { StudioState } from '../../state/studioState';
import { Icon } from '../Icon';
function Readout({ name, symbol, value }: { name: string; symbol?: string; value?: number }) {
  return <div className="readout"><span>{symbol && <i>{symbol}</i>}{name}</span><strong>{value === undefined ? '—' : value.toPrecision(6)}</strong></div>;
}
export function Inspector({ state }: { state: StudioState }) {
  const point=state.selected;
  return <aside className="inspector-panel panel" aria-labelledby="inspector-heading">
    <div className="panel-heading"><Icon name="crosshair" /><h2 id="inspector-heading">Inspector</h2></div>
    <div className="inspector-content">
      <div className="point-status"><span className="small-dot" />{point ? 'Mock cell selected' : 'No point selected'}</div>
      <p className="inspector-description">Click the current preview to inspect the containing cell center.</p>
      <div className="section-label">COORDINATES</div><Readout name="x" value={point?.x} /><Readout name="y" value={point?.y} />
      <div className="section-label readout-heading">FIELD VALUES</div><Readout name="Density" symbol="ρ" value={point?.density} /><Readout name="Temperature" symbol="T" value={point?.temperature} /><Readout name="Pressure" symbol="P" value={point?.pressure} />
      <div className="section-label readout-heading">CONTEXT</div><Readout name="Composition" symbol="Xᵢ" /><Readout name="AMR level" />
      <p className="section-note">Dimensionless Mock values only. Composition and AMR are not modeled.</p>
    </div>
    <div className="inspector-bottom"><Icon name="crosshair" size={24} /><span>Select a point.<br />Understand the field.</span></div>
  </aside>;
}
