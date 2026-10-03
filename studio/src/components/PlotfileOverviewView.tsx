import {useEffect,useId,useRef,useState} from 'react';
import type {PointerEvent as ReactPointerEvent} from 'react';
import type {AuditResponse} from '../host/plotfileAudit';
import {finitePlotRange,panPlotView,plotFraction,plotValue,zoomPlotView} from '../data/nativePlotView';
import type {PlotView} from '../data/nativePlotView';
import {plotColor} from '../data/plotColors';

const frame={left:72,top:20,width:790,height:290};
function fraction(e:{clientX:number;clientY:number},svg:SVGSVGElement){
 const b=svg.getBoundingClientRect();
 return {x:((e.clientX-b.left)/b.width*900-frame.left)/frame.width,
  y:1-((e.clientY-b.top)/b.height*370-frame.top)/frame.height};
}
export function PlotfileOverviewView(props:{samples:AuditResponse;disabled:boolean;onInspect:(index:number)=>void}){
 const o=props.samples.audit.overview;if(!o)return null;
 return <Overview key={[props.samples.audit.file.sha256,o.field,o.width,o.height].join(':')} {...props}/>;
}
function Overview({samples,disabled,onInspect}:{samples:AuditResponse;disabled:boolean;onInspect:(index:number)=>void}){
 const o=samples.audit.overview!,range=finitePlotRange(o.values.map(v=>v===null?'NaN':v));
 const domain:PlotView={x:o.domain.x,y:o.dimension===2?o.domain.y:range??[0,1]};
 const [view,setView]=useState(domain),[chosen,setChosen]=useState<number|null>(null);
 const svgRef=useRef<SVGSVGElement|null>(null),drag=useRef<{x:number;y:number;clientX:number;clientY:number;view:PlotView}|null>(null);
 const clipId=useId().replaceAll(':','');
 useEffect(()=>{
  const svg=svgRef.current;if(!svg)return;
  function wheel(e:WheelEvent){
   const f=fraction(e,svg!);if(f.x<0||f.x>1||f.y<0||f.y>1)return;
   e.preventDefault();setView(v=>zoomPlotView(v,e.deltaY>0?1.15:1/1.15,f.x,f.y));
  }
  svg.addEventListener('wheel',wheel,{passive:false});
  return ()=>svg.removeEventListener('wheel',wheel);
 },[]);
 const X=(x:number)=>frame.left+plotFraction(x,view.x)*frame.width;
 const Y=(y:number)=>frame.top+(1-plotFraction(y,view.y))*frame.height;
 const dx=(o.domain.x[1]-o.domain.x[0])/o.width,dy=(o.domain.y[1]-o.domain.y[0])/o.height;
 function select(pixel:number){
  const index=o.representativeIndices[pixel];if(index===null||disabled)return;
  setChosen(pixel);onInspect(index);
 }
 function down(e:ReactPointerEvent<SVGSVGElement>){
  if(e.button!==0)return;const f=fraction(e,e.currentTarget);
  if(f.x<0||f.x>1||f.y<0||f.y>1)return;
  e.currentTarget.setPointerCapture(e.pointerId);
  drag.current={...f,clientX:e.clientX,clientY:e.clientY,view};
 }
 function move(e:ReactPointerEvent<SVGSVGElement>){
  const d=drag.current;if(!d)return;const f=fraction(e,e.currentTarget);
  setView(panPlotView(d.view,f.x-d.x,f.y-d.y));
 }
 function up(e:ReactPointerEvent<SVGSVGElement>){
  const d=drag.current;drag.current=null;if(!d)return;
  if(e.currentTarget.hasPointerCapture(e.pointerId))e.currentTarget.releasePointerCapture(e.pointerId);
  if(Math.hypot(e.clientX-d.clientX,e.clientY-d.clientY)>3)return;
  setView(d.view);const f=fraction(e,e.currentTarget);
  if(f.x<0||f.x>1||f.y<0||f.y>1)return;
  const x=plotValue(f.x,d.view.x),y=plotValue(f.y,d.view.y);
  const i=Math.floor((x-o.domain.x[0])/dx),j=o.dimension===2?Math.floor((y-o.domain.y[0])/dy):0;
  if(i>=0&&i<o.width&&j>=0&&j<o.height)select(j*o.width+i);
 }
 return <section aria-label="Candidate global Plotfile display LOD">
  <h3>Global display LOD · candidate</h3>
  <p>{samples.relativePath} · {o.field} · time {samples.audit.time} · file {samples.audit.file.sha256}</p>
  <p>{o.scannedCells} stored leaf cells scanned → {o.width}×{o.height} display pixels. Coordinate-overlap-weighted display means; not native values or scientific integrals. Units remain unknown.</p>
  <p>Click a pixel to read its largest-overlap representative native cell. Inspector shows that stored cell, not the LOD mean. Zoom/pan redraw this existing LOD; zoom does not fetch finer data.</p>
  <button onClick={()=>setView(v=>zoomPlotView(v,.8))}>Zoom in · LOD</button>
  <button onClick={()=>setView(v=>zoomPlotView(v,1.25))}>Zoom out · LOD</button>
  <button onClick={()=>setView(domain)}>Fit full domain · LOD</button>
  {o.values.some(v=>v===null)&&<p role="alert">Magenta pixels / 1D gaps mean empty, nonfinite or overflowed display reduction; raw Inspector remains available.</p>}
  <svg ref={svgRef} viewBox="0 0 900 370" preserveAspectRatio="none" aria-label="Global Plotfile LOD plot"
   style={{width:'100%',height:370,touchAction:'none',background:'#14232c'}}
   onPointerDown={down} onPointerMove={move} onPointerUp={up}
   onPointerCancel={()=>{const d=drag.current;drag.current=null;if(d)setView(d.view);}}>
   <defs><clipPath id={clipId}><rect x={frame.left} y={frame.top} width={frame.width} height={frame.height}/></clipPath></defs>
   <rect x={frame.left} y={frame.top} width={frame.width} height={frame.height} fill="none" stroke="#84949f"/>
   <g clipPath={'url(#'+clipId+')'}>
    {o.values.map((value,pixel)=>{
     const i=pixel%o.width,j=Math.floor(pixel/o.width),x=o.domain.x[0]+i*dx,y=o.domain.y[0]+j*dy;
     const chosenPixel=chosen===pixel;
     const keyboard={role:'button',tabIndex:disabled?-1:0,'aria-label':'Inspect representative native cell for LOD pixel '+pixel,
      'aria-pressed':chosenPixel,onKeyDown:(e:React.KeyboardEvent<SVGElement>)=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();select(pixel);}}};
     if(o.dimension===1)return value===null?null:<line key={pixel} {...keyboard}
      x1={X(x)} x2={X(x+dx)} y1={Y(value)} y2={Y(value)} stroke={chosenPixel?'#ffde59':'#65d2ff'} strokeWidth={chosenPixel?5:3}/>;
     return <rect key={pixel} {...keyboard} x={X(x)} y={Y(y+dy)}
      width={dx/(view.x[1]-view.x[0])*frame.width} height={dy/(view.y[1]-view.y[0])*frame.height}
      fill={value===null||!range?'#ff00ff':plotColor('Viridis',plotFraction(value,range))}
      stroke={chosenPixel?'#ffde59':'none'} strokeWidth={2}/>;
    })}
   </g>
   {[0,.25,.5,.75,1].map(f=><g key={f} fill="#d5e2eb" fontSize="11">
    <text x={frame.left+f*frame.width} y={335} textAnchor="middle">{plotValue(f,view.x).toPrecision(4)}</text>
    <text x={65} y={frame.top+(1-f)*frame.height+4} textAnchor="end">{plotValue(f,view.y).toPrecision(4)}</text>
   </g>)}
   <text x={460} y={363} textAnchor="middle" fill="#d5e2eb">x1 · unit unknown</text>
   <text x={16} y={165} transform="rotate(-90 16 165)" textAnchor="middle" fill="#d5e2eb">{o.dimension===2?'x2 · unit unknown':o.field+' · display mean'}</text>
  </svg>
  {o.dimension===2&&range&&<p>Viridis display means: {range[0]} → {range[1]} · unit unknown.</p>}
  <p>Native AMR block outlines are not yet included in this LOD view. {o.diagnostics.join(' · ')}</p>
 </section>;
}
