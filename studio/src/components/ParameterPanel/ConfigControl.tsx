import { NumericInput } from './NumericInput';
import { enumControls, rawOption } from './controlContract';
import type { ParameterMeta } from '../../data/parSchema';
export function ConfigControl({ name, value, meta, error, onChange }: { name: string; value: string; meta?: ParameterMeta; error?: string; onChange: (value: string) => void }) {
  const options=enumControls[name] ?? meta?.options;
  const raw=options ? rawOption(value,options) : null;
  const unitRange=meta?.range?.[0] === 0 && meta.range[1] === 1;
  return <>{unitRange && <><input type="range" title="Range 0–1; slider coarse step 0.001. Numeric input preserves exact precision." aria-invalid={!!error} aria-label={`${name} slider`} min="0" max="1" step="0.001" value={Number.isFinite(Number(value)) ? Number(value) : 0} onChange={e=>onChange(e.target.value)} /></>}
    {meta?.type === 'bool' && /^(true|false)$/i.test(value) ? <input type="checkbox" aria-label={name} aria-invalid={!!error} title="ARCH bool: true / false (case-insensitive)" checked={value.toLowerCase()==='true'} onChange={e=>onChange(String(e.target.checked))} /> : options ? <select aria-label={name} aria-invalid={!!error} value={value} onChange={e=>onChange(e.target.value)}>{raw && <option value={raw.value}>{raw.label}</option>}{options.map(option=><option key={option} value={option}>{option}</option>)}</select> : meta?.type==='int' || meta?.type==='float' ? <NumericInput name={name} value={value} error={error} integer={meta.type==='int'} range={meta.range} onChange={onChange} /> : <input type="text" aria-label={name} value={value} aria-invalid={!!error} onChange={e=>onChange(e.target.value)} />}

  </>;
}
