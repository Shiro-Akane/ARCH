import type {CoreBinding} from '../../host/previewContracts';
import { useEffect, useMemo, useRef } from 'react';
import { LineVis, Annotation, useCanvasEvent, useVisCanvasContext, useCameraState } from '@h5web/lib';
import ndarray from 'ndarray';
import '@h5web/lib/styles.css';
import {lineDomain} from '../../data/LinePreviewData';
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

export function LineRenderer({ data, selected, onPoint, marker }: { marker?:MarkerProps; data: LinePreviewData; selected: number | null; onPoint: (x: number) => void }) {
  const array = useMemo(() => ndarray(data.values, [data.values.length]), [data]);
  const domain=useMemo(()=>lineDomain(data),[data]);
  return <div className="line-renderer" aria-label={`${data.field} line plot`}>
    <LineVis dataArray={array} domain={domain} abscissaParams={{ label: 'x', value: data.x }} ordinateLabel={data.field}><SampleSelection data={data} selected={selected} onPoint={onPoint} />{marker&&<PositionMarker {...marker}/>}</LineVis>
  </div>;
}

interface MarkerProps {binding:CoreBinding;value:number;pending:boolean;onCandidate:(value:number|null)=>void;onCommit:(value:number)=>void}
function PositionMarker({binding,value,pending,onCandidate,onCommit}:MarkerProps){
 const context=useVisCanvasContext();const camera=useCameraState(c=>c,[]);
 const drag=useRef<{value:number;cleanup:()=>void}|null>(null);
 useEffect(()=>()=>{drag.current?.cleanup();},[]);
 const domains=context.getVisibleDomains(camera);
 if(!Number.isFinite(value))return null;
 const invalid=value<=binding.min||value>=binding.max;
 return <Annotation x={value} y={(domains.yVisibleDomain[0]+domains.yVisibleDomain[1])/2} center><button type="button" aria-label="Drag x_pos marker" title={invalid?'Outside Core bounds':`x_pos ${value} · ${pending?'pending':'current'}`} style={{height:context.canvasSize.height*0.8,width:18,border:'none',background:'linear-gradient(to right, transparent 8px, #ffbf66 8px, #ffbf66 10px, transparent 10px)',color:'#ffbf66',cursor:'ew-resize',touchAction:'none',pointerEvents:'auto'}} onPointerDown={e=>{
  if(e.button!==0)return;e.preventDefault();e.stopPropagation();
  const start=value;
  const finish=(commit:boolean)=>{const d=drag.current;if(!d)return;d.cleanup();drag.current=null;onCandidate(null);if(commit&&d.value!==start&&d.value>binding.min&&d.value<binding.max)onCommit(d.value);};
  const move=(event:PointerEvent)=>{event.preventDefault();const rect=context.canvasArea.getBoundingClientRect();const x=context.getVisibleDomains(camera).xVisibleDomain;const next=x[0]+(event.clientX-rect.left)/rect.width*(x[1]-x[0]);if(drag.current){drag.current.value=next;onCandidate(next);}};
  const up=()=>finish(true);const cancel=()=>finish(false);const key=(event:KeyboardEvent)=>{if(event.key==='Escape'){event.preventDefault();event.stopPropagation();cancel();}};
  const cleanup=()=>{window.removeEventListener('pointermove',move);window.removeEventListener('pointerup',up);window.removeEventListener('pointercancel',cancel);window.removeEventListener('keydown',key,true);window.removeEventListener('blur',cancel);};
  drag.current={value,cleanup};window.addEventListener('pointermove',move);window.addEventListener('pointerup',up);window.addEventListener('pointercancel',cancel);window.addEventListener('keydown',key,true);window.addEventListener('blur',cancel);
 }}><span style={{position:'absolute',top:0,left:4,whiteSpace:'nowrap'}}>x_pos {value.toPrecision(5)} {invalid?'out of range':pending?'pending':''}</span></button></Annotation>;
}
