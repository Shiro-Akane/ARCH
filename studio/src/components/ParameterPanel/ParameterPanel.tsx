import type { ReactNode } from 'react';
import type { Parameters, ParameterKey } from '../../state/studioState';
import { validate } from '../../state/studioState';
import { Icon } from '../Icon';

function Group({ title, badge, children, open = false, custom = false }: { title: string; badge?: string; children: ReactNode; open?: boolean; custom?: boolean }) {
  return <details className={custom ? 'parameter-group custom-group' : 'parameter-group'} open={open}>
    <summary><Icon name="chevron" size={12} /><span>{title}</span>{badge && <span className="group-badge">{badge}</span>}</summary>
    <div className="group-content">{children}</div>
  </details>;
}
function Field({ label, parameters, onEdit }: { label: ParameterKey; parameters: Parameters; onEdit: (key: ParameterKey, value: string) => void }) {
  const error = validate(parameters)[label];
  const options = label === 'backend' ? ['CPU', 'CUDA'] : label === 'eos_type' ? ['ideal'] : null;
  return <label className="parameter-field"><span>{label}</span>
    {options ? <select aria-label={label} value={parameters[label]} onChange={e => onEdit(label, e.target.value)}>{options.map(option => <option key={option}>{option}</option>)}</select>
      : <input aria-label={label} aria-invalid={!!error} aria-describedby={error ? `${label}-error` : undefined} inputMode="decimal" value={parameters[label]} onChange={e => onEdit(label, e.target.value)} />}
    {error && <small className="validation-error" id={`${label}-error`}>{error}</small>}
  </label>;
}
export function ParameterPanel({ parameters, onEdit }: { parameters: Parameters; onEdit: (key: ParameterKey, value: string) => void }) {
  return <aside className="parameter-panel panel" aria-labelledby="parameters-heading">
    <div className="panel-heading"><Icon name="layers" /><h2 id="parameters-heading">Parameters</h2><span className="micro-label">DEMO</span></div>
    <div className="parameter-scroll">
      <div className="section-label">CONFIGURATION <span>Working copy</span></div>
      <Group title="Grid" badge="2D">
        <div className="paired-fields"><Field label="resolution_x" parameters={parameters} onEdit={onEdit} /><Field label="resolution_y" parameters={parameters} onEdit={onEdit} /></div>
        <div className="paired-fields"><Field label="xmin" parameters={parameters} onEdit={onEdit} /><Field label="xmax" parameters={parameters} onEdit={onEdit} /></div>
        <div className="paired-fields"><Field label="ymin" parameters={parameters} onEdit={onEdit} /><Field label="ymax" parameters={parameters} onEdit={onEdit} /></div>
      </Group>
      <Group title="EOS" badge="Ideal"><Field label="eos_type" parameters={parameters} onEdit={onEdit} /></Group>
      <Group title="Network" badge="None"><p className="section-note">No network is connected in this demo.</p></Group>
      <Group title="Runtime"><Field label="tmax" parameters={parameters} onEdit={onEdit} /><Field label="backend" parameters={parameters} onEdit={onEdit} /></Group>
      <div className="section-label custom-label">CUSTOM PARAMS <span>Hotspot</span></div>
      <Group title="Hotspot" badge="4" open custom>
        <div className="paired-fields"><Field label="hotspot_x" parameters={parameters} onEdit={onEdit} /><Field label="hotspot_y" parameters={parameters} onEdit={onEdit} /></div>
        <Field label="hotspot_radius" parameters={parameters} onEdit={onEdit} />
        <Field label="hotspot_temperature" parameters={parameters} onEdit={onEdit} />
        <p className="section-note">Illustrative settings only. No physical units or solver connection.</p>
      </Group>
    </div>
    <div className="panel-note"><span className="small-dot" />Demo configuration · no real file writes.</div>
  </aside>;
}
