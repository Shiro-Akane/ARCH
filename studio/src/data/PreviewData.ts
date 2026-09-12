export interface PreviewData {
  width: number; height: number;
  xRange: [number, number]; yRange: [number, number];
  field: string; min: number; max: number; values: Float32Array;
  metadata?: Record<string, unknown>;
}
export type PreviewFields = Record<'density' | 'temperature' | 'pressure', PreviewData>;
