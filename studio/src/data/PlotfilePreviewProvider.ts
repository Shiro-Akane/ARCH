import h5wasm from 'h5wasm';
import { withLocalHdf5 } from './localHdf5.ts';
import { adaptUniformLine } from './LinePreviewData.ts';
import type { LinePreviewData } from './LinePreviewData.ts';

export interface PlotfileMetadata {
  file: string;
  time: number;
  dimension: 1;
  geometry: string;
  fields: string[];
}

function scalar(value: unknown): unknown {
  if (Array.isArray(value) && value.length === 1) return value[0];
  return value;
}

export function discoverPlotfile(handle: InstanceType<typeof h5wasm.File>, file: string): PlotfileMetadata {
  const attrs = handle.attrs;
  const time = scalar(attrs.time?.json_value);
  const dimension = scalar(attrs.dim?.json_value);
  const geometry = scalar(attrs.geometry?.json_value);
  if (typeof time !== 'number' || !Number.isFinite(time) || typeof geometry !== 'string' || !geometry.trim() || typeof dimension !== 'number') {
    throw new Error('Unsupported ARCH plotfile: missing or invalid time, dim or geometry.');
  }
  if (dimension !== 1) throw new Error('Unsupported plotfile: only simple 1D data is supported.');
  const data = handle.get('Data');
  if (!(data instanceof h5wasm.Group)) throw new Error('Unsupported ARCH plotfile: missing Data group.');
  const fields = data.keys().filter(name => data.get(name) instanceof h5wasm.Dataset);
  if (!fields.length) throw new Error('Unsupported ARCH plotfile: no field datasets.');
  return { file, time, dimension, geometry, fields };
}

export async function inspectPlotfile(file: File): Promise<PlotfileMetadata> {
  return withLocalHdf5(file, handle => discoverPlotfile(handle, file.name));
}

// Bound expanded dataset size as well as file size (compressed HDF5 can be small).
function numeric(dataset: unknown, label: string): Float64Array {
  if (!(dataset instanceof h5wasm.Dataset) || ![0, 1].includes(dataset.metadata.type)) {
    throw new Error(`Unsupported numeric dataset: ${label}.`);
  }
  const shape = dataset.shape;
  const count = shape?.reduce((a, b) => a * b, 1) ?? 0;
  if (!shape || shape.length < 1 || shape.length > 2 || count < 1 || count > 1000000) throw new Error(`Unsupported dataset size: ${label}.`);
  const value = dataset.value;
  if (!ArrayBuffer.isView(value) || value instanceof DataView || value instanceof BigInt64Array || value instanceof BigUint64Array) throw new Error(`Unsupported numeric type: ${label}.`);
  return Float64Array.from(value as ArrayLike<number>);
}

export async function readPlotfileField(file: File, field: string): Promise<LinePreviewData> {
  return withLocalHdf5(file, handle => {
    const info = discoverPlotfile(handle, file.name);
    if (!info.fields.includes(field)) throw new Error('Field is not present in this plotfile.');
    const grid = handle.get('Grid');
    const data = handle.get('Data');
    if (!(grid instanceof h5wasm.Group) || !(data instanceof h5wasm.Group)) throw new Error('Unsupported ARCH plotfile: missing Grid.');
    return adaptUniformLine(field, numeric(grid.get('x'), 'x coordinates'), numeric(data.get(field), field));
  });
}
