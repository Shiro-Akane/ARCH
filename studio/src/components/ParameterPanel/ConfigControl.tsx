import type { ParameterMeta } from '../../data/parSchema';
export function ConfigControl({ name, value, meta, error, onChange }: { name: string; value: string; meta?: ParameterMeta; error?: string; onChange: (value: string) => void }) {
  const unitRange=meta?.range?.[0] === 0 && meta.range[1] === 1;
  return <>{unitRange && <><input type="range" aria-label={`${name} slider`} min="0" max="1" step="0.001" value={Number.isFinite(Number(value)) ? Number(value) : 0} onChange={e=>onChange(e.target.value)} /><small>0–1 · slider coarse step 0.001; exact value below</small></>}
    {meta?.type === 'bool' && /^(true|false)$/i.test(value) ? <input type="checkbox" aria-label={name} checked={value.toLowerCase()==='true'} onChange={e=>onChange(String(e.target.checked))} /> : meta?.options ? <select aria-label={name} value={value.toLowerCase()} onChange={e=>onChange(e.target.value)}>{!meta.options.includes(value.toLowerCase()) && <option value={value.toLowerCase()}>{value} (unsupported)</option>}{meta.options.map(option=><option key={option}>{option}</option>)}</select> : <input type={meta?.type==='int' || meta?.type==='float' ? 'number' : 'text'} step={meta?.type==='int' ? 1 : 'any'} aria-label={name} inputMode={meta?.type === 'int' || meta?.type === 'float' ? 'decimal' : 'text'} value={value} aria-invalid={!!error} onChange={e=>onChange(e.target.value)} />}
  </>;
}
