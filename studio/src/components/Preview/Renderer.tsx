import { useMemo, useRef } from 'react';
import { HeatmapVis, Annotation, useCanvasEvent, Html } from '@h5web/lib';
import ndarray from 'ndarray';
import '@h5web/lib/styles.css';
import type { PreviewData } from '../../data/PreviewData';

import type { SelectedPoint } from '../../data/selection';
function PointSelection({ selected, onPoint, enabled }: { selected: SelectedPoint | null; onPoint: (x:number,y:number)=>void; enabled:boolean }) {
  const down=useRef<{x:number;y:number}|null>(null);
  useCanvasEvent('mousedown', ({sourceEvent}) => { down.current = sourceEvent.button === 0 ? {x:sourceEvent.clientX,y:sourceEvent.clientY} : null; });
  useCanvasEvent('mouseup', ({dataPt,sourceEvent}) => {
    const start=down.current; down.current=null;
    if(enabled && start && sourceEvent.button===0 && Math.hypot(sourceEvent.clientX-start.x,sourceEvent.clientY-start.y)<4) onPoint(dataPt.x,dataPt.y);
  });
  return selected ? <Annotation x={selected.x} y={selected.y} center><span className="selected-point" role="img" aria-label="Selected mock cell" /></Annotation> : null;
}
function CanvasStatus({message}:{message:string}) {
  return <Html overflowCanvas><div className="canvas-status-layer"><div className="preview-overlay" role="status">{message}</div></div></Html>;
}
// Adapter consumes only PreviewData and display interaction props.
export function Renderer({ data, selected, onPoint, enabled, status }: { status?:string; data: PreviewData; selected: SelectedPoint | null; onPoint: (x:number,y:number)=>void; enabled:boolean }) {
  const array = useMemo(() => ndarray(data.values, [data.height, data.width]), [data]);
  const x = useMemo(() => Float64Array.from({length:data.width}, (_, i) => data.xRange[0] + (i + 0.5) / data.width * (data.xRange[1] - data.xRange[0])), [data]);
  const y = useMemo(() => Float64Array.from({length:data.height}, (_, i) => data.yRange[0] + (i + 0.5) / data.height * (data.yRange[1] - data.yRange[0])), [data]);
  return <div className="heatmap-renderer" aria-label={`${data.field} Mock heatmap`}>
    <HeatmapVis dataArray={array} domain={[data.min, data.max === data.min ? data.min + 1 : data.max]} colorMap="Viridis" abscissaParams={{label:'x · demo',value:x}} ordinateParams={{label:'y · demo',value:y}} renderTooltip={() => <></>}>{status && <CanvasStatus message={status} />}<PointSelection selected={selected} onPoint={onPoint} enabled={enabled} /></HeatmapVis>
  </div>;
}
