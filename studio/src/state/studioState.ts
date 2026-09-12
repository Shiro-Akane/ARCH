import { inspectPoint } from '../data/selection.ts';
import type { SelectedPoint } from '../data/selection';
import type { PreviewFields } from '../data/PreviewData';
export type ConfigState = 'saved' | 'dirty' | 'invalid';
export type PreviewState = 'current' | 'stale' | 'generating' | 'failed';
export type RunState = 'idle';
export const defaults = {
  resolution_x: '512', resolution_y: '512', xmin: '0', xmax: '1', ymin: '0', ymax: '1',
  eos_type: 'ideal', tmax: '0.15', backend: 'CPU',
  hotspot_x: '0.5', hotspot_y: '0.5', hotspot_radius: '0.12', hotspot_temperature: '1',
};
export type Parameters = typeof defaults;
export type ParameterKey = keyof Parameters;
export function validate(p: Parameters): Partial<Record<ParameterKey, string>> {
  const errors: Partial<Record<ParameterKey, string>> = {};
  for (const key of Object.keys(p) as ParameterKey[]) {
    if (key === 'eos_type' || key === 'backend') continue;
    if (!p[key].trim() || !Number.isFinite(Number(p[key]))) errors[key] = 'Enter a finite number.';
  }
  for (const key of ['resolution_x', 'resolution_y'] as const) {
    const n = Number(p[key]);
    if (!Number.isInteger(n) || n < 2 || n > 512) errors[key] = 'Use an integer from 2 to 512 (demo limit).';
  }
  for (const [low, high] of [['xmin', 'xmax'], ['ymin', 'ymax']] as const) {
    if (!(Number(p[high]) > Number(p[low])) || !Number.isFinite(Number(p[high]) - Number(p[low]))) errors[high] = 'Maximum must exceed minimum with a finite span.';
  }
  for (const key of ['hotspot_radius', 'hotspot_temperature', 'tmax'] as const) {
    if (!(Number(p[key]) > 0)) errors[key] = 'Must be greater than zero.';
  }
  if (p.eos_type !== 'ideal') errors.eos_type = 'Demo supports ideal only.';
  if (!['CPU', 'CUDA'].includes(p.backend)) errors.backend = 'Choose CPU or CUDA (demo only).';
  return errors;
}
export interface StudioState {
  previewParameters: Parameters | null; selected: SelectedPoint | null; field: keyof PreviewFields; data: PreviewFields | null; working: Parameters; saved: Parameters; config: ConfigState;
  preview: PreviewState; run: RunState; revision: number; request: number | null; error: string | null;
}
export function initialState(): StudioState {
  return { previewParameters: null, selected: null, field: 'density', data: null, working: { ...defaults }, saved: { ...defaults }, config: 'saved', preview: 'stale', run: 'idle', revision: 0, request: null, error: null };
}
export type Action =
  | { type: 'config/save' }
  | { type: 'config/revert' }
  | { type: 'point/select'; x: number; y: number }
  | { type: 'field/select'; field: keyof PreviewFields }
  | { type: 'edit'; key: ParameterKey; value: string }
  | { type: 'preview/start'; revision: number }
  | { type: 'preview/success'; revision: number; data?: PreviewFields }
  | { type: 'preview/failure'; revision: number; message: string };
export function studioReducer(state: StudioState, action: Action): StudioState {
  if (action.type === 'config/save') {
    if (state.config === 'invalid') return state;
    return { ...state, saved: { ...state.working }, config: 'saved' };
  }
  if (action.type === 'config/revert') {
    const matchesPreview = state.previewParameters !== null && (Object.keys(state.saved) as ParameterKey[]).every(key => state.saved[key] === state.previewParameters![key]);
    return { ...state, working: { ...state.saved }, config: 'saved', preview: matchesPreview && state.data ? 'current' : 'stale', selected: null, request: null, error: null, revision: state.revision + 1 };
  }
  if (action.type === 'point/select') return state.data && state.preview === 'current' ? { ...state, selected: inspectPoint(state.data, action.x, action.y) } : state;
  if (action.type === 'field/select') return { ...state, field: action.field };
  if (action.type === 'edit') {
    if (state.working[action.key] === action.value) return state;
    const working = { ...state.working, [action.key]: action.value };
    return { ...state, selected: null, working, config: Object.keys(validate(working)).length ? 'invalid' : 'dirty', preview: 'stale', revision: state.revision + 1, request: null, error: null };
  }
  if (action.type === 'preview/start') {
    if (state.config === 'invalid' || state.preview === 'generating' || action.revision !== state.revision) return state;
    return { ...state, selected: null, preview: 'generating', request: action.revision, error: null };
  }
  // Discard late results from requests invalidated by editing.
  if (state.request !== action.revision || state.revision !== action.revision || state.preview !== 'generating') return state;
  return { ...state, previewParameters: action.type === 'preview/success' ? { ...state.working } : state.previewParameters, data: action.type === 'preview/success' ? action.data ?? state.data : state.data, preview: action.type === 'preview/success' ? 'current' : 'failed', request: null, error: action.type === 'preview/failure' ? action.message : null };
}
