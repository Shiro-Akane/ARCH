import type { PreviewFields } from './PreviewData';
export interface SelectedPoint { x: number; y: number; index: number; density: number; temperature: number; pressure: number }
export function inspectPoint(data: PreviewFields, x: number, y: number): SelectedPoint | null {
  const d=data.density;
  if (!Number.isFinite(x) || !Number.isFinite(y) || x<d.xRange[0] || x>d.xRange[1] || y<d.yRange[0] || y>d.yRange[1]) return null;
  const col=Math.min(d.width-1,Math.floor((x-d.xRange[0])/(d.xRange[1]-d.xRange[0])*d.width));
  const row=Math.min(d.height-1,Math.floor((y-d.yRange[0])/(d.yRange[1]-d.yRange[0])*d.height));
  const index=row*d.width+col;
  return {index,x:d.xRange[0]+(col+0.5)/d.width*(d.xRange[1]-d.xRange[0]),y:d.yRange[0]+(row+0.5)/d.height*(d.yRange[1]-d.yRange[0]),density:data.density.values[index],temperature:data.temperature.values[index],pressure:data.pressure.values[index]};
}
