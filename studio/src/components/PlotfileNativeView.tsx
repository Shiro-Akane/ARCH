import {useEffect,useId,useRef,useState} from 'react';
import type {PointerEvent as ReactPointerEvent} from 'react';
import type {AuditResponse} from '../host/plotfileAudit';
import {plotColor} from '../data/plotColors';
import {finitePlotRange,nativePlotDomain,panNativeSpatialView,pickNativePlotCell,plotFraction,plotValue,zoomNativeSpatialView} from '../data/nativePlotView';
import type {PlotView} from '../data/nativePlotView';

const frame={left:72,top:20,width:790,height:290};
function fractions(e:{clientX:number;clientY:number},svg:SVGSVGElement){
  const b=svg.getBoundingClientRect();
  return {x:( (e.clientX-b.left)/b.width*900-frame.left)/frame.width,
   y:1-((e.clientY-b.top)/b.height*370-frame.top)/frame.height};
 }

export function PlotfileNativeView({samples,selectedRow,onSelect}:{samples:AuditResponse;selectedRow:number|null;onSelect:(row:number)=>void}){
 const a=samples.audit,p=a.payload,n=p?.nativeCells;
 if(!p||!n||a.geometry!=='cartesian'||![1,2].includes(a.dimension))return null;
 const domain=nativePlotDomain(n,a.dimension,p.values);
 if(!domain)return <p role="alert">No finite display range. Raw samples remain available in the table.</p>;
 // A successful replacement resets its viewport; retained failed/cancelled data keep theirs.
 const identity=[a.file.sha256,p.field,p.block,...p.start,'shape',...p.shape].join(':');
 return <NativeView key={identity} samples={samples} domain={domain} selectedRow={selectedRow} onSelect={onSelect}/>;
}
function NativeView({samples,domain,selectedRow,onSelect}:{samples:AuditResponse;domain:PlotView;selectedRow:number|null;onSelect:(row:number)=>void}){
 const a=samples.audit,p=a.payload!,n=p.nativeCells!;
 const [view,setView]=useState(domain);
 const svgRef=useRef<SVGSVGElement|null>(null);
 useEffect(()=>{
  const svg=svgRef.current;if(!svg)return;
  function wheel(e:WheelEvent){
   const f=fractions(e,svg!);if(f.x<0||f.x>1||f.y<0||f.y>1)return;
   e.preventDefault();setView(v=>zoomNativeSpatialView(v,a.dimension,e.deltaY>0?1.15:1/1.15,f.x,f.y));
  }
  svg.addEventListener('wheel',wheel,{passive:false});
  return ()=>svg.removeEventListener('wheel',wheel);
 },[a.dimension]);
 const drag=useRef<{x:number;y:number;view:PlotView;clientX:number;clientY:number}|null>(null);
 const clipId=useId().replaceAll(':','');
 const fieldRange=finitePlotRange(p.values);
 const X=(v:number)=>frame.left+plotFraction(v,view.x)*frame.width;
 const Y=(v:number)=>frame.top+(1-plotFraction(v,view.y))*frame.height;
 function down(e:ReactPointerEvent<SVGSVGElement>){
  if(e.button!==0)return;
  const f=fractions(e,e.currentTarget);if(f.x<0||f.x>1||f.y<0||f.y>1)return;
  e.currentTarget.setPointerCapture(e.pointerId);
  drag.current={...f,view,clientX:e.clientX,clientY:e.clientY};
 }
 function move(e:ReactPointerEvent<SVGSVGElement>){
  const d=drag.current;if(!d)return;
  const f=fractions(e,e.currentTarget);setView(panNativeSpatialView(d.view,a.dimension,f.x-d.x,f.y-d.y));
 }
 function up(e:ReactPointerEvent<SVGSVGElement>){
  const d=drag.current;drag.current=null;if(!d)return;
  if(e.currentTarget.hasPointerCapture(e.pointerId))e.currentTarget.releasePointerCapture(e.pointerId);
  if(Math.hypot(e.clientX-d.clientX,e.clientY-d.clientY)>3)return;
  setView(d.view);
  const f=fractions(e,e.currentTarget);
  if(f.x<0||f.x>1||f.y<0||f.y>1)return;
  const row=pickNativePlotCell(n,a.dimension,plotValue(f.x,d.view.x),plotValue(f.y,d.view.y));
  if(row!==null)onSelect(row);
 }
 const invalid=p.values.filter(v=>typeof v!=='number').length;
 return <section aria-label="Candidate native Plotfile slice view">
  <h3>Native stored cells · candidate slice display</h3>
  <p>One requested block region, not a global overview or LOD. Units and scientific certification remain pending. Field: {p.field} · level {n.level} · key {n.logicalKey}.</p>
  <div>
   <button onClick={()=>setView(v=>zoomNativeSpatialView(v,a.dimension,.8))}>Zoom in · stored cells</button>
   <button onClick={()=>setView(v=>zoomNativeSpatialView(v,a.dimension,1.25))}>Zoom out · stored cells</button>
   <button onClick={()=>setView(domain)}>Fit slice</button>
  </div>
  <p>Drag to pan; scroll to zoom; click a cell or use its keyboard focus to inspect. Display operations perform no read, Save or Preview.</p>
  {invalid>0&&<p role="alert">{invalid} nonfinite stored value(s): magenta in 2D, gaps in 1D; raw values remain unchanged.</p>}
  <svg ref={svgRef} viewBox="0 0 900 370" preserveAspectRatio="none" style={{width:'100%',height:370,touchAction:'none',background:'#14232c'}}
   aria-label="Native stored cell plot" onPointerDown={down} onPointerMove={move} onPointerUp={up}
   onPointerCancel={()=>{const d=drag.current;drag.current=null;if(d)setView(d.view);}}
>
   <defs><clipPath id={clipId}><rect x={frame.left} y={frame.top} width={frame.width} height={frame.height}/></clipPath></defs>
   <rect x={frame.left} y={frame.top} width={frame.width} height={frame.height} fill="none" stroke="#84949f"/>
   <g clipPath={'url(#'+clipId+')'}>
    {p.values.map((value,i)=>{
     const finite=typeof value==='number',chosen=selectedRow===i;
     const selectProps={tabIndex:0,role:'button', 'aria-label':'Inspect plotted cell '+p.linearIndices[i],
      'aria-pressed':chosen,onKeyDown:(e:React.KeyboardEvent<SVGElement>)=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();onSelect(i);}}};
     if(a.dimension===1){
      if(!finite)return null;
      return <line key={i} {...selectProps} x1={X(n.lower.x1[i])} x2={X(n.upper.x1[i])} y1={Y(value)} y2={Y(value)}
       stroke={chosen?'#ffde59':'#65d2ff'} strokeWidth={chosen?5:3}/>;
     }
     const fraction=finite&&fieldRange?plotFraction(value,fieldRange):0;
     return <rect key={i} {...selectProps} x={X(n.lower.x1[i])} y={Y(n.upper.x2[i])}
      width={(n.upper.x1[i]-n.lower.x1[i])/(view.x[1]-view.x[0])*frame.width}
      height={(n.upper.x2[i]-n.lower.x2[i])/(view.y[1]-view.y[0])*frame.height}
      fill={finite?plotColor('Viridis',fraction):'#ff00ff'} stroke={chosen?'#ffde59':'#617580'} strokeWidth={chosen?3:.4}/>;
    })}
   </g>
   {[0,.25,.5,.75,1].map(f=><g key={f} fill="#d5e2eb" fontSize="11">
    <text x={frame.left+f*frame.width} y={335} textAnchor="middle">{plotValue(f,view.x).toPrecision(4)}</text>
    <text x={65} y={frame.top+(1-f)*frame.height+4} textAnchor="end">{plotValue(f,view.y).toPrecision(4)}</text>
   </g>)}
   <text x={460} y={363} textAnchor="middle" fill="#d5e2eb">x1 · unit unknown</text>
   <text x={16} y={165} transform="rotate(-90 16 165)" textAnchor="middle" fill="#d5e2eb">{a.dimension===2?'x2 · unit unknown':p.field+' · unit unknown'}</text>
  </svg>
  {a.dimension===2&&fieldRange&&<div aria-label="Stored field color range" style={{background:'linear-gradient(to right,'+[0,.25,.5,.75,1].map(t=>plotColor('Viridis',t)).join(',')+')',padding:6,color:'#fff'}}>
   Viridis · {fieldRange[0]} → {fieldRange[1]} · {p.field} · unit unknown
  </div>}
 </section>;
}
