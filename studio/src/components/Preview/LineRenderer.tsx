import { useMemo, useRef } from 'react';
import { LineVis, Annotation, useCanvasEvent } from '@h5web/lib';
import ndarray from 'ndarray';
import '@h5web/lib/styles.css';
import type { LinePreviewData } from '../../data/LinePreviewData';

function SampleSelection({ data, selected, onPoint }: { data: LinePreviewData; selected: number | null; onPoint: (x: number) => void }) {
  const down = useRef<{ x: number; y: number } | null>(null);
  useCanvasEvent('mousedown', ({ sourceEvent }) => { down.current = sourceEvent.button === 0 ? { x: sourceEvent.clientX, y: sourceEvent.clientY } : null; });
  useCanvasEvent('mouseup', ({ sourceEvent, dataPt }) => {
    const start = down.current; down.current = null;
    if (start && sourceEvent.button === 0 && Math.hypot(sourceEvent.clientX - start.x, sourceEvent.clientY - start.y) < 4) onPoint(dataPt.x);
  });
  return selected === null ? null : <Annotation x={data.x[selected]} y={data.values[selected]} center><span className="selected-point" role="img" aria-label="Selected real sample" /></Annotation>;
}

export function LineRenderer({ data, selected, onPoint }: { data: LinePreviewData; selected: number | null; onPoint: (x: number) => void }) {
  const array = useMemo(() => ndarray(data.values, [data.values.length]), [data]);
  return <div className="line-renderer" aria-label={`${data.field} line plot`}>
    <LineVis dataArray={array} domain={undefined} abscissaParams={{ label: 'x', value: data.x }} ordinateLabel={data.field}><SampleSelection data={data} selected={selected} onPoint={onPoint} /></LineVis>
  </div>;
}
