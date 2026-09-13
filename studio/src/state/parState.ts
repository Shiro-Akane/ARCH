import { parSchema } from '../data/parSchema.ts';
import { parsePar, serializePar, effectiveEntries } from '../data/ParDocument.ts';
import type { ParDocument } from '../data/ParDocument.ts';
function validExpression(value: string): boolean {
  const text=value.replace(/\s/g,'');
  const numeric='[+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?';
  if (new RegExp(`^${numeric}$`).test(text)) return Number.isFinite(Number(text));
  if (text==='pi' || text==='-pi') return true;
  const left=text.match(new RegExp(`^(${numeric})\\*pi$`));
  const right=text.match(new RegExp(`^pi([*/])(${numeric})$`));
  if(left) return Number.isFinite(Number(left[1])*Math.PI);
  if(right) return Number.isFinite(right[1]==='*' ? Math.PI*Number(right[2]) : Math.PI/Number(right[2]));
  return false;
}
export interface ParState { filename: string; document: ParDocument; changes: Record<string,string> }
export function loadPar(filename: string, raw: string): ParState {
  const document = parsePar(raw);
  if (!document.entries.length) throw new Error('No ARCH key=value entries found.');
  return { filename, document, changes: Object.create(null) };
}
export function parErrors(state: ParState): Record<string,string> {
  const errors: Record<string,string> = Object.create(null);
  for (const entry of effectiveEntries(state.document)) {
    const value = state.changes[entry.key] ?? entry.value;
    const meta = parSchema[entry.key];
    if (meta?.range && (!value.trim() || !Number.isFinite(Number(value)) || Number(value)<meta.range[0] || Number(value)>meta.range[1])) errors[entry.key] = 'Must be a finite number in [0, 1]; value was not clamped.';
    if (meta?.type === 'expression' && !validExpression(value)) errors[entry.key]='Use a finite number, pi, -pi, coefficient*pi, pi*coefficient or pi/coefficient; ambiguous forms are unsupported.';
    if (meta?.type === 'int' && (!/^[+-]?\d+$/.test(value) || !Number.isSafeInteger(Number(value)) || Number(value)<-2147483648 || Number(value)>2147483647)) errors[entry.key]='Enter a complete 32-bit integer; ambiguous numeric suffixes are unsupported.';
    if (meta?.type === 'float' && (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(value) || !Number.isFinite(Number(value)))) errors[entry.key]='Enter a finite decimal or scientific-notation number; ambiguous expressions/suffixes are unsupported here.';
    if (meta?.type === 'bool' && !/^(true|false)$/i.test(value)) errors[entry.key]='Expected true or false (case-insensitive).';
    if (meta?.options && !meta.options.includes(value.toLowerCase())) errors[entry.key]=`Choose ${meta.options.join(', ')}.`;
    if (!entry.key) errors[entry.key] = 'Empty key is ambiguous and cannot be edited.';
    if (/[\r\n#\0]/.test(value) || value !== value.trim()) errors[entry.key] = 'Value must not contain comments, newlines or surrounding whitespace.';
  }
  const values = Object.fromEntries(effectiveEntries(state.document).map(e => [e.key,state.changes[e.key] ?? e.value]));
  const refine = Number(values.refine_threshold ?? '0.8'), derefine = Number(values.derefine_threshold ?? '0.2');
  if (derefine < 0 || derefine >= refine) errors.derefine_threshold = 'Requires 0 <= derefine_threshold < refine_threshold (including defaults 0.2 / 0.8).';
  if (values.regrid_interval !== undefined && Number(values.regrid_interval)<1) errors.regrid_interval='Must be positive.';
  if (Number(values.nblockx2 ?? 1)<=0 && Number(values.nblockx3 ?? 1)>0) errors.nblockx3='nblockx3 must be <= 0 when nblockx2 <= 0.';
  if (values.restart?.toLowerCase()==='true' && !(values.restart_file ?? '').trim()) errors.restart_file='restart_file is required when restart=true.';
  for (const key of ['sml_rho','min_eint','nseTempThreshold']) if (values[key]!==undefined && !(Number(values[key])>0)) errors[key]='Must be positive.';
  if (values.nseDensThreshold!==undefined && Number(values.nseDensThreshold)<0) errors.nseDensThreshold='Must be nonnegative.';
  if (Number(values.max_eint ?? '1e21') < Number(values.min_eint ?? '1e-10')) errors.max_eint='Must not be smaller than min_eint.';
  return errors;
}
export function parStatus(state: ParState): 'saved'|'dirty'|'invalid' {
  if (Object.keys(parErrors(state)).length) return 'invalid';
  return serializePar(state.document,state.changes) === state.document.raw ? 'saved' : 'dirty';
}
export function editPar(state: ParState, key: string, value: string): ParState { return { ...state, changes: Object.assign(Object.create(null), state.changes, { [key]: value }) }; }
export function revertPar(state: ParState): ParState { return { ...state, changes: Object.create(null) }; }

export function exportPar(state: ParState): { filename: string; text: string } {
  if (parStatus(state) === 'invalid') throw new Error('Correct invalid parameters before exporting.');
  return { filename: state.filename.replace(/\.par$/i,'') + '_modified.par', text: serializePar(state.document,state.changes) };
}
