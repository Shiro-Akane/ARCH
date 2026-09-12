import { validate } from '../state/studioState.ts';
import type { Parameters } from '../state/studioState.ts';
import type { PreviewData, PreviewFields } from './PreviewData';

// Illustrative dimensionless fields only; no ARCH equations or files are used.
export function buildMockPreview(parameters: Parameters): PreviewFields {
  if (Object.keys(validate(parameters)).length) throw new Error('Correct invalid parameters before generating.');
  const n = Object.fromEntries(Object.entries(parameters).map(([key, value]) => [key, Number(value)]));
  const width = n.resolution_x, height = n.resolution_y;
  const xRange: [number, number] = [n.xmin, n.xmax], yRange: [number, number] = [n.ymin, n.ymax];
  const create = (field: string): PreviewData => ({ width, height, xRange, yRange, field, min: Infinity, max: -Infinity, values: new Float32Array(width * height), metadata: { source: 'Mock / Demo', scientific: false, units: 'dimensionless', layout: 'row-major, y increasing', sampling: 'cell centers' } });
  const fields: PreviewFields = { density: create('Density'), temperature: create('Temperature'), pressure: create('Pressure') };
  for (let row = 0; row < height; row++) {
    const y = n.ymin + (row + 0.5) / height * (n.ymax - n.ymin);
    for (let col = 0; col < width; col++) {
      const x = n.xmin + (col + 0.5) / width * (n.xmax - n.xmin);
      const distance = Math.hypot((x - n.hotspot_x) / n.hotspot_radius, (y - n.hotspot_y) / n.hotspot_radius);
      const spot = Math.exp(-0.5 * distance * distance);
      const index = row * width + col;
      const samples = { density: 1 + n.hotspot_temperature * spot, temperature: 0.2 + (col + 0.5) / width * 0.6 + n.hotspot_temperature * spot, pressure: x < n.hotspot_x ? 1.0 : 0.2 };
      for (const key of ['density', 'temperature', 'pressure'] as const) {
        const value = Math.fround(samples[key]);
        if (!Number.isFinite(value)) throw new Error('Parameters exceed the finite Float32 demo range.');
        const field = fields[key]; field.values[index] = value;
        field.min = Math.min(field.min, value); field.max = Math.max(field.max, value);
      }
    }
  }
  return fields;
}
export async function generateMockPreview(parameters: Parameters, signal: AbortSignal): Promise<PreviewFields> {
  await new Promise<void>((resolve, reject) => {
    if (signal.aborted) { reject(new Error('Preview cancelled')); return; }
    const cancel = () => { clearTimeout(timer); reject(new Error('Preview cancelled')); };
    const timer = setTimeout(() => { signal.removeEventListener('abort', cancel); resolve(); }, 500);
    signal.addEventListener('abort', cancel, { once: true });
  });
  return buildMockPreview(parameters);
}
