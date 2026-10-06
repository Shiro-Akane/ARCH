import {isRetiredParameter} from '../data/retiredParameters.ts';
import type {StandardParameter} from '../host/configurationContracts.ts';
import {standardValueError} from '../data/standardValidation.ts';
import { parSchema } from '../data/parSchema.ts';
import { parsePar, serializePar, effectiveEntries } from '../data/ParDocument.ts';
import type { ParDocument } from '../data/ParDocument.ts';
function validExpression(value: string): boolean {
  return standardValueError({type:'expression',constraints:{}},value)===undefined;
}
export interface ParState { filename: string; document: ParDocument; changes: Record<string,string>; removedKeys?:string[] }
export function loadPar(filename: string, raw: string): ParState {
  const document = parsePar(raw);
  return { filename, document, changes: Object.create(null) };
}
export function parErrors(state: ParState, schema?:readonly StandardParameter[]): Record<string,string> {
  const errors: Record<string,string> = Object.create(null);
  for (const entry of [...effectiveEntries(state.document).filter(e=>!state.removedKeys?.includes(e.key)),...Object.keys(state.changes).filter(key=>!state.document.entries.some(e=>e.key===key)).map(key=>({key,value:state.changes[key]}))]) {
    if(isRetiredParameter(entry.key)){errors[entry.key]='RETIRED_PARAMETER: explicit removal required; not a custom parameter or alias.';continue;}
    const value = state.changes[entry.key] ?? entry.value;
    const meta = parSchema[entry.key];
    if (meta?.range && (!value.trim() || !Number.isFinite(Number(value)) || Number(value)<meta.range[0] || Number(value)>meta.range[1])) errors[entry.key] = 'Must be a finite number in [0, 1]; value was not clamped.';
    if (meta?.type === 'expression' && !validExpression(value)) errors[entry.key]='Use a finite number, pi, -pi, coefficient*pi, pi*coefficient, pi/coefficient or exp(number); ambiguous forms are unsupported.';
    if (meta?.type === 'int' && (!/^[+-]?\d+$/.test(value) || !Number.isSafeInteger(Number(value)) || Number(value)<-2147483648 || Number(value)>2147483647)) errors[entry.key]='Enter a complete 32-bit integer; ambiguous numeric suffixes are unsupported.';
    if (meta?.type === 'float' && (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(value) || !Number.isFinite(Number(value)))) errors[entry.key]='Enter a finite decimal or scientific-notation number; ambiguous expressions/suffixes are unsupported here.';
    if (meta?.type === 'bool' && !/^(true|false)$/i.test(value)) errors[entry.key]='Expected true or false (case-insensitive).';
    if (meta?.options && !meta.options.includes(value.toLowerCase())) errors[entry.key]=`Choose ${meta.options.join(', ')}.`;
    const standard=schema?.find(p=>p.key===entry.key);
    if(standard){delete errors[entry.key];const error=standardValueError(standard,value);if(error)errors[entry.key]=error;}
    if (!entry.key) errors[entry.key] = 'Empty key is ambiguous and cannot be edited.';
    if (/[\r\n#\0]/.test(value) || value !== value.trim()) errors[entry.key] = 'Value must not contain comments, newlines or surrounding whitespace.';
  }
  for(const [key,value] of Object.entries(state.changes))if(/[\r\n#\0]/.test(value)||value!==value.trim())errors[key]='Value must not contain comments, newlines or surrounding whitespace.';
  const values = {...Object.fromEntries(effectiveEntries(state.document).filter(e=>!state.removedKeys?.includes(e.key)).map(e => [e.key,e.value])),...state.changes};
  const refine = values.refine_threshold===undefined?undefined:Number(values.refine_threshold), derefine = values.derefine_threshold===undefined?undefined:Number(values.derefine_threshold);
  if (derefine!==undefined && (derefine < 0 || refine!==undefined && derefine >= refine)) errors.derefine_threshold = 'Requires 0 <= derefine_threshold < refine_threshold.';
  if (values.regrid_interval !== undefined && Number(values.regrid_interval)<1) errors.regrid_interval='Must be positive.';
  if (values.nblockx2!==undefined && values.nblockx3!==undefined && Number(values.nblockx2)<=0 && Number(values.nblockx3)>0) errors.nblockx3='nblockx3 must be <= 0 when nblockx2 <= 0.';
  if (values.restart?.toLowerCase()==='true' && !(values.restart_file ?? '').trim()) errors.restart_file='restart_file is required when restart=true.';
  for (const key of ['sml_rho','min_eint','nseTempThreshold']) if (values[key]!==undefined && !(Number(values[key])>0)) errors[key]='Must be positive.';
  if (values.nseDensThreshold!==undefined && Number(values.nseDensThreshold)<0) errors.nseDensThreshold='Must be nonnegative.';
  if (values.max_eint!==undefined && values.min_eint!==undefined && Number(values.max_eint) < Number(values.min_eint)) errors.max_eint='Must not be smaller than min_eint.';
  return errors;
}
export function parStatus(state: ParState, schema?:readonly StandardParameter[]): 'saved'|'dirty'|'invalid' {
  if (Object.keys(parErrors(state,schema)).length) return 'invalid';
  return serializePar(state.document,state.changes,state.removedKeys) === state.document.raw ? 'saved' : 'dirty';
}
export function editPar(state: ParState, key: string, value: string): ParState { if(isRetiredParameter(key))throw new Error('Retired parameter cannot be edited or inserted.'); return { ...state, removedKeys:state.removedKeys?.filter(k=>k!==key), changes: Object.assign(Object.create(null), state.changes, { [key]: value }) }; }
export function revertPar(state: ParState): ParState { return { ...state, changes: Object.create(null), removedKeys:[] }; }

export function exportPar(state: ParState, schema?:readonly StandardParameter[]): { filename: string; text: string } {
  if (parStatus(state,schema) === 'invalid') throw new Error('Correct invalid parameters before exporting.');
  return { filename: state.filename.replace(/\.par$/i,'') + '_modified.par', text: serializePar(state.document,state.changes,state.removedKeys) };
}

export function removeRetiredParameter(state:ParState,key:string):ParState{
 if(!isRetiredParameter(key))throw new Error('Only retired parameters can use this migration action.');
 const changes=Object.assign(Object.create(null),state.changes);delete changes[key];
 return {...state,changes,removedKeys:[...new Set([...(state.removedKeys??[]),key])]};
}

export function removeForbiddenParameter(state:ParState,key:string,authorizedKeys:ReadonlySet<string>):ParState{
 if(!authorizedKeys.has(key))throw new Error('Matching Core diagnostic required for removal.');
 const changes=Object.assign(Object.create(null),state.changes);delete changes[key];
 return {...state,changes,removedKeys:[...new Set([...(state.removedKeys??[]),key])]};
}

/** Persist an explicit draft without granting scientific execution eligibility.
 * The lossless serializer still rejects structurally unsafe edits.
 */
export function exportDraft(state:ParState):{filename:string;text:string}{
 return {filename:state.filename.replace(/\.par$/i,'')+'_modified.par',
         text:serializePar(state.document,state.changes,state.removedKeys)};
}
