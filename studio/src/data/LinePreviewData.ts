export interface LinePreviewData {
  kind: 'line';
  field: string;
  x: Float64Array;
  values: Float64Array;
  min: number;
  max: number;
}

export function adaptUniformLine(field: string, x: Float64Array, values: Float64Array): LinePreviewData {
  if (!x.length || x.length !== values.length) throw new Error('Unsupported field: empty or mismatched coordinates.');
  if (!x.every(Number.isFinite) || !values.every(Number.isFinite)) throw new Error('Unsupported field: NaN or Infinity.');
  const order = Array.from(x.keys()).sort((a, b) => x[a] - x[b]);
  const sortedX = Float64Array.from(order, i => x[i]);
  const sortedValues = Float64Array.from(order, i => values[i]);
  const step = sortedX.length > 1 ? sortedX[1] - sortedX[0] : 1;
  const tolerance = Math.max(Math.abs(step) * 1e-6, Number.EPSILON * Math.max(1, ...[sortedX[0], sortedX[sortedX.length - 1]].map(Math.abs)) * 16);
  for (let i = 1; i < sortedX.length; i++) {
    const delta = sortedX[i] - sortedX[i - 1];
    if (delta <= 0 || Math.abs(delta - step) > tolerance) throw new Error('Unsupported grid: requires unique uniform 1D coordinates; no AMR reconstruction.');
  }
  let min = Infinity, max = -Infinity;
  for (const value of sortedValues) { min = Math.min(min, value); max = Math.max(max, value); }
  return { kind: 'line', field, x: sortedX, values: sortedValues, min, max };
}

export function nearestSample(data: LinePreviewData, x: number): number | null {
  if (!Number.isFinite(x) || x < data.x[0] || x > data.x[data.x.length - 1]) return null;
  let lo = 0, hi = data.x.length - 1;
  while (lo < hi) { const mid = Math.floor((lo + hi) / 2); if (data.x[mid] < x) lo = mid + 1; else hi = mid; }
  return lo > 0 && x - data.x[lo - 1] <= data.x[lo] - x ? lo - 1 : lo;
}
