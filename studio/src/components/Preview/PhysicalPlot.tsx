import {useId,useLayoutEffect,useMemo,useRef,useState,useEffect} from 'react';
import type {PointerEvent as ReactPointerEvent} from 'react';
import {plotColor} from '../../data/plotColors';
import type {CoreBinding} from '../../host/previewContracts';
import {sampleEdges} from '../../data/RealInitPreviewProvider';
import {axisDefault,fieldDefault,displayRange,fieldRange,projection,zoomView,panView,paddedRange,logDataError} from '../../data/plotPresentation';
import type {Range} from '../../data/plotPresentation';
import {PlotControls} from './PlotControls';
export interface PositionMarkerProps {binding:CoreBinding;value:number;pending:boolean;onCandidate:(value:number|null)=>void;onCommit:(value:number)=>void}
interface Props {xDomain?:Range;yDomain?:Range;x:Float64Array;y?:Float64Array;values:Float64Array;field:string;selected:number|null;onPoint:(x:number,y:number)=>void;marker?:PositionMarkerProps}
const FULL:Range=[0,1];
const format=(n:number)=>n!==0&&(Math.abs(n)>=1e5||Math.abs(n)<1e-3)?n.toExponential(3):Number(n.toPrecision(5)).toString();
export function PhysicalPlot({xDomain,yDomain,x,y,values,field,selected,onPoint,marker}:Props){
 const [xs,setX]=useState(axisDefault),[ys,setY]=useState(axisDefault),[fs,setField]=useState(fieldDefault);
 const [view,setView]=useState<{x:Range;y:Range}>({x:FULL,y:FULL});
 const [size,setSize]=useState({width:700,height:420});const container=useRef<HTMLDivElement>(null),canvas=useRef<HTMLCanvasElement>(null);
 const drag=useRef<{id:number;px:number;py:number;view:typeof view;marker:boolean;value:number;startValue:number;moved:boolean}|null>(null);
 const markerRef=useRef(marker);useLayoutEffect(()=>{markerRef.current=marker;},[marker]);
 useEffect(()=>{const cancel=()=>{if(drag.current?.marker)markerRef.current?.onCandidate(null);drag.current=null;};const key=(e:KeyboardEvent)=>{if(e.key==='Escape')cancel();};window.addEventListener('keydown',key);window.addEventListener('blur',cancel);return()=>{cancel();window.removeEventListener('keydown',key);window.removeEventListener('blur',cancel);};},[]);
 useLayoutEffect(()=>{const el=container.current;if(!el)return;const observer=new ResizeObserver(entries=>{const r=entries[0].contentRect;setSize({width:Math.max(240,r.width),height:Math.max(200,r.height)});});observer.observe(el);const prevent=(e:WheelEvent)=>{const r=el.getBoundingClientRect();if(e.clientX>=r.left+80&&e.clientX<=r.right-90&&e.clientY>=r.top+24&&e.clientY<=r.bottom-58)e.preventDefault();};el.addEventListener('wheel',prevent,{passive:false});return()=>{observer.disconnect();el.removeEventListener('wheel',prevent);};},[]);
 const edgesX=useMemo(()=>sampleEdges(x),[x]);const edgesY=useMemo(()=>y?sampleEdges(y):null,[y]);
 const extent=useMemo(()=>{let min=Infinity,max=-Infinity;for(const v of values){min=Math.min(min,v);max=Math.max(max,v);}return paddedRange(min,max);},[values]);
 const geometry=useMemo(()=>{
  try{
   for(const error of [logDataError(x,xs.scale,'X coordinates'),y&&logDataError(y,ys.scale,'Y coordinates'),logDataError(values,fs.scale,'Field values')])if(error)throw Error(error);
   const xr=displayRange(xs,xDomain??[edgesX[0],edgesX[edgesX.length-1]]),fr=fieldRange(fs,extent);
   const yr=y&&edgesY?displayRange(ys,yDomain??[edgesY[0],edgesY[edgesY.length-1]]):fr;
   return {xr,yr,fr,xp:projection(xr,xs.scale,view.x),yp:projection(yr,y?ys.scale:fs.scale,view.y),fp:projection(fr,fs.scale)};
  }catch(e){return {error:e instanceof Error?e.message:'Invalid display settings'};}
 },[xDomain,yDomain,x,y,values,xs,ys,fs,edgesX,edgesY,extent,view]);
 const left=80,top=24,width=Math.max(80,size.width-left-90),height=Math.max(80,size.height-top-58);
 useLayoutEffect(()=>{
  const el=canvas.current,ctx=el?.getContext('2d');if(!el||!ctx)return;const ratio=window.devicePixelRatio||1;
  el.width=Math.round(size.width*ratio);el.height=Math.round(size.height*ratio);ctx.setTransform(ratio,0,0,ratio,0,0);ctx.clearRect(0,0,size.width,size.height);
  if('error' in geometry)return;const {xp,yp,fp,xr,yr}=geometry;
  ctx.save();ctx.beginPath();ctx.rect(left,top,width,height);ctx.clip();
  if(y&&edgesY){
   for(let j=0;j<y.length;j++)for(let i=0;i<x.length;i++){
    const a=Math.max(xr[0],edgesX[i]),b=Math.min(xr[1],edgesX[i+1]),c=Math.max(yr[0],edgesY[j]),d=Math.min(yr[1],edgesY[j+1]);if(a>=b||c>=d)continue;
    const x0=left+xp.forward(a)*width,x1=left+xp.forward(b)*width,y0=top+(1-yp.forward(d))*height,y1=top+(1-yp.forward(c))*height;
    ctx.fillStyle=plotColor(fs.map,Math.max(0,Math.min(1,fp.forward(values[j*x.length+i]))));ctx.fillRect(Math.floor(x0),Math.floor(y0),Math.ceil(x1)-Math.floor(x0),Math.ceil(y1)-Math.floor(y0));
   }
  }else{
   ctx.strokeStyle=plotColor(fs.map,.8);ctx.lineWidth=2;ctx.beginPath();
   for(let i=0;i<x.length;i++){const px=left+xp.forward(x[i])*width,py=top+(1-yp.forward(values[i]))*height;if(i===0)ctx.moveTo(px,py);else ctx.lineTo(px,py);}ctx.stroke();
   // Explicit edge indicators for values excluded by the chosen display range.
   ctx.fillStyle='#ffbf66';for(let i=0;i<x.length;i++)if(values[i]<yr[0]||values[i]>yr[1]){const px=left+xp.forward(x[i])*width;ctx.fillRect(px-1,values[i]<yr[0]?top+height-4:top,2,4);}
  }
  ctx.restore();
 },[geometry,size,width,height,x,y,values,edgesX,edgesY,fs.map]);
 const clipId=useId();
 function coordinates(e:{clientX:number;clientY:number;currentTarget:SVGSVGElement}){const r=e.currentTarget.getBoundingClientRect();return {px:(e.clientX-r.left)*size.width/r.width,py:(e.clientY-r.top)*size.height/r.height};}
 function fractions(px:number,py:number){return {x:(px-left)/width,y:1-(py-top)/height};}
 const inside=(px:number,py:number)=>px>=left&&px<=left+width&&py>=top&&py<=top+height;
 function down(e:ReactPointerEvent<SVGSVGElement>){if(e.button!==0||'error' in geometry)return;const {px,py}=coordinates(e);if(!inside(px,py))return;e.preventDefault();const isMarker=(e.target as Element).closest('[data-position-marker]')!==null;e.currentTarget.setPointerCapture(e.pointerId);drag.current={id:e.pointerId,px,py,view,marker:isMarker,value:marker?.value??NaN,startValue:marker?.value??NaN,moved:false};}
 function move(e:ReactPointerEvent<SVGSVGElement>){const d=drag.current;if(!d||d.id!==e.pointerId||'error' in geometry)return;const {px,py}=coordinates(e);if(Math.hypot(px-d.px,py-d.py)>3)d.moved=true;
  if(d.marker&&marker){d.value=geometry.xp.inverse((px-left)/width);marker.onCandidate(d.value);}else if(d.moved)setView({x:panView(d.view.x,(px-d.px)/width),y:panView(d.view.y,-(py-d.py)/height)});
 }
 function up(e:ReactPointerEvent<SVGSVGElement>){const d=drag.current;if(!d)return;drag.current=null;if(e.currentTarget.hasPointerCapture(e.pointerId))e.currentTarget.releasePointerCapture(e.pointerId);
  if(d.marker&&marker){marker.onCandidate(null);if(d.moved&&d.value!==d.startValue&&d.value>marker.binding.min&&d.value<marker.binding.max)marker.onCommit(d.value);}else if(!d.moved&&!('error' in geometry)){const {px,py}=coordinates(e);if(inside(px,py)){const f=fractions(px,py);onPoint(geometry.xp.inverse(f.x),geometry.yp.inverse(f.y));}}
 }
 function fit(){setView({x:FULL,y:FULL});setX(axisDefault());setY(axisDefault());setField(f=>({...f,manual:false}));}
 const sx=selected===null?NaN:x[selected%x.length],sy=selected===null?NaN:y?y[Math.floor(selected/x.length)]:values[selected];
 const gradient=Array.from({length:17},(_,i)=>({offset:`${i/16*100}%`,color:plotColor(fs.map,i/16)}));
 return <><PlotControls twoD={!!y} x={xs} y={ys} field={fs} onX={s=>{setX(s);setView(v=>({...v,x:FULL}));}} onY={s=>{setY(s);setView(v=>({...v,y:FULL}));}} onField={s=>{setField(s);if(!y)setView(v=>({...v,y:FULL}));}}/>
 <button onClick={fit}>Fit {y?'2D':'1D'} view</button><p className="section-note">Scroll to zoom · drag to pan · click to inspect. Fit restores full coordinate domain. Raw samples are unchanged.</p>
 {(fs.lower||fs.upper)&&<p role="status">Clipped · {fs.lower?`lower ${fs.low}`:''} {fs.upper?`upper ${fs.high}`:''} · {y?'colorbar endpoint saturation':'out-of-range portions marked at plot edges'}</p>}
 {marker&&(!Number.isFinite(marker.value)||marker.value<=marker.binding.min||marker.value>=marker.binding.max)&&<p role="status">x_pos candidate outside Core bounds ({marker.binding.min}, {marker.binding.max}); release will not commit. Correct the value or cancel the drag.</p>}
 {'error' in geometry&&<p role="alert" className="plot-error">{geometry.error} Plot withheld; Inspector retains raw values.</p>}
 <div ref={container} className="physical-plot" aria-label={`${field} ${y?'real 2D heatmap':'line plot'}`}><canvas ref={canvas} aria-hidden="true"/>
 <svg viewBox={`0 0 ${size.width} ${size.height}`} aria-label="Physical plot interaction surface" onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerCancel={()=>{drag.current=null;marker?.onCandidate(null);}} onWheel={e=>{if('error' in geometry||drag.current||e.deltaY===0)return;const {px,py}=coordinates(e);if(!inside(px,py))return;e.stopPropagation();const f=fractions(px,py),factor=e.deltaY<0?.8:1.25;setView(v=>({x:zoomView(v.x,f.x,factor),y:zoomView(v.y,f.y,factor)}));}}>
 <defs><clipPath id={clipId}><rect x={left} y={top} width={width} height={height}/></clipPath><linearGradient id={`${clipId}-color`} x1="0" y1="1" x2="0" y2="0">{gradient.map(s=><stop key={s.offset} offset={s.offset} stopColor={s.color}/>)}</linearGradient></defs>
 {!('error' in geometry)&&<><rect x={left} y={top} width={width} height={height} fill="none" stroke="#647686"/>{Array.from({length:6},(_,i)=>{const f=i/5;return <g key={i} className="physical-tick"><line x1={left+f*width} x2={left+f*width} y1={top} y2={top+height} stroke="#8899aa" opacity=".18"/><text x={left+f*width} y={top+height+22} textAnchor="middle">{format(geometry.xp.inverse(f))}</text><line x1={left} x2={left+width} y1={top+(1-f)*height} y2={top+(1-f)*height} stroke="#8899aa" opacity=".18"/><text x={left-10} y={top+(1-f)*height+4} textAnchor="end">{format(geometry.yp.inverse(f))}</text></g>;})}<text x={left+width/2} y={size.height-7} textAnchor="middle">x1 · {xs.scale}</text><text transform={`translate(14 ${top+height/2}) rotate(-90)`} textAnchor="middle">{y?`x2 · ${ys.scale}`:`${field} · ${fs.scale}`}</text>
 <g clipPath={`url(#${clipId})`}>{selected!==null&&Number.isFinite(sx)&&Number.isFinite(sy)&&<g><circle cx={left+geometry.xp.forward(sx)*width} cy={top+(1-geometry.yp.forward(sy))*height} r={8} fill="none" stroke="#15212b" strokeWidth={3} pointerEvents="none"/><circle role="img" aria-label={y?'Selected real 2D sample':'Selected real sample'} cx={left+geometry.xp.forward(sx)*width} cy={top+(1-geometry.yp.forward(sy))*height} r={6} fill="none" stroke="white" strokeWidth={2} pointerEvents="none"/></g>}
 {marker&&Number.isFinite(marker.value)&&!(xs.scale==='log'&&marker.value<=0)&&<g data-position-marker="true" role="button" aria-label="Drag x_pos marker" style={{cursor:'ew-resize'}}><line x1={left+geometry.xp.forward(marker.value)*width} x2={left+geometry.xp.forward(marker.value)*width} y1={top} y2={top+height} stroke="transparent" strokeWidth={18}/><line x1={left+geometry.xp.forward(marker.value)*width} x2={left+geometry.xp.forward(marker.value)*width} y1={top} y2={top+height} stroke="#ffbf66" strokeWidth={2}/><text x={left+geometry.xp.forward(marker.value)*width+7} y={top+18}>x_pos {format(marker.value)} {marker.pending?'pending':''}</text></g>}</g>
 {y&&<g aria-label={`${fs.map} ${fs.scale} colorbar`}><rect x={left+width+16} y={top} width={14} height={height} fill={`url(#${clipId}-color)`}/>{[0,.5,1].map(f=><text key={f} x={left+width+34} y={top+(1-f)*height+4}>{format(geometry.fp.inverse(f))}</text>)}</g>}</>}
 </svg></div></>;
}
