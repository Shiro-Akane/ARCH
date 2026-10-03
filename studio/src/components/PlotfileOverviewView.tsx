import {useEffect,useId,useRef,useState} from 'react';
import type {PointerEvent as ReactPointerEvent} from 'react';
import type {AuditResponse} from '../host/plotfileAudit';
import {finitePlotRange,linePlotRange,panNativeSpatialView,plotFraction,plotValue,zoomNativeSpatialView} from '../data/nativePlotView';
import type {PlotfileDomain} from '../host/plotfileOverview';
import type {PlotView} from '../data/nativePlotView';
import {plotColor} from '../data/plotColors';

const frame={left:72,top:20,width:790,height:290};
function fraction(e:{clientX:number;clientY:number},svg:SVGSVGElement){
 const b=svg.getBoundingClientRect();
 return {x:((e.clientX-b.left)/b.width*900-frame.left)/frame.width,
  y:1-((e.clientY-b.top)/b.height*370-frame.top)/frame.height};
}
export function PlotfileOverviewView(props:{samples:AuditResponse;disabled:boolean;onInspect:(index:number)=>void;onPoint:(point:number[])=>void;fullSamples?:AuditResponse;onRefine?:(viewport:PlotfileDomain)=>void;onFitFull?:()=>void;onViewChange?:()=>void}){
 const o=props.samples.audit.overview;if(!o)return null;
 return <Overview key={[props.samples.audit.file.sha256,o.field,o.width,o.height].join(':')} {...props}/>;
}
function Overview({samples,disabled,onInspect,onPoint,fullSamples,onRefine,onFitFull,onViewChange}:{samples:AuditResponse;disabled:boolean;onInspect:(index:number)=>void;onPoint:(point:number[])=>void;fullSamples?:AuditResponse;onRefine?:(viewport:PlotfileDomain)=>void;onFitFull?:()=>void;onViewChange?:()=>void}){
 const o=samples.audit.overview!,range=finitePlotRange(o.values.map(v=>v===null?'NaN':v));
 const domain:PlotView={x:o.domain.x,y:o.dimension===2?o.domain.y:linePlotRange(o.values.map(v=>v===null?'NaN':v))??[0,1]};
 const full=fullSamples?.audit.file.sha256===samples.audit.file.sha256&&fullSamples.audit.overview?.field===o.field?fullSamples.audit.overview:o;
 const fullRange=linePlotRange(full.values.map(v=>v===null?'NaN':v));
 const fullView:PlotView={x:full.domain.x,y:o.dimension===2?full.domain.y:fullRange??[0,1]};
 const [view,setView]=useState(domain),[chosen,setChosen]=useState<number|null>(null);
 const [showBlocks,setShowBlocks]=useState(true),[hiddenLevels,setHiddenLevels]=useState<number[]>([]);
 const [selectedBlock,setSelectedBlock]=useState<number|null>(null);
 const leaves=o.nativeBlocks,levels=[...new Set(leaves?.blocks.map(b=>b.level)??[])].sort((a,b)=>a-b);
 const selectedLeaf=leaves?.blocks.find(b=>b.index===selectedBlock);
 const svgRef=useRef<SVGSVGElement|null>(null),drag=useRef<{x:number;y:number;clientX:number;clientY:number;view:PlotView}|null>(null);
 const clipId=useId().replaceAll(':','');
 useEffect(()=>{
  const svg=svgRef.current;if(!svg)return;
  function wheel(e:WheelEvent){
   const f=fraction(e,svg!);if(f.x<0||f.x>1||f.y<0||f.y>1)return;
   e.preventDefault();onViewChange?.();setView(v=>zoomNativeSpatialView(v,o.dimension,e.deltaY>0?1.15:1/1.15,f.x,f.y));
  }
  svg.addEventListener('wheel',wheel,{passive:false});
  return ()=>svg.removeEventListener('wheel',wheel);
 },[onViewChange,o.dimension]);
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
  onViewChange?.();e.currentTarget.setPointerCapture(e.pointerId);
  drag.current={...f,clientX:e.clientX,clientY:e.clientY,view};
 }
 function move(e:ReactPointerEvent<SVGSVGElement>){
  const d=drag.current;if(!d)return;const f=fraction(e,e.currentTarget);
  onViewChange?.();setView(panNativeSpatialView(d.view,o.dimension,f.x-d.x,f.y-d.y));
 }
 function up(e:ReactPointerEvent<SVGSVGElement>){
  const d=drag.current;drag.current=null;if(!d)return;
  if(e.currentTarget.hasPointerCapture(e.pointerId))e.currentTarget.releasePointerCapture(e.pointerId);
  if(Math.hypot(e.clientX-d.clientX,e.clientY-d.clientY)>3)return;
  setView(d.view);const f=fraction(e,e.currentTarget);
  if(f.x<0||f.x>1||f.y<0||f.y>1)return;
  const x=plotValue(f.x,d.view.x),y=plotValue(f.y,d.view.y);
  const i=Math.floor((x-o.domain.x[0])/dx),j=o.dimension===2?Math.floor((y-o.domain.y[0])/dy):0;
  if(disabled)return;
  if(i>=0&&i<o.width&&j>=0&&j<o.height)setChosen(j*o.width+i);
  onPoint(o.dimension===1?[x]:[x,y]);
 }
 return <section aria-label="Candidate global Plotfile display LOD">
  <h3>Plotfile display LOD · candidate</h3>
  <p>{samples.relativePath} · {o.field} · time {samples.audit.time} · file {samples.audit.file.sha256}</p>
  <p>{o.globalDomain&&JSON.stringify(o.domain)!==JSON.stringify(o.globalDomain)?'Viewport LOD':'Full-domain LOD'} · displayed x1 [{o.domain.x.join(', ')}]{o.dimension===2?' · x2 ['+o.domain.y.join(', ')+']':''}</p>
  <p>{o.scannedCells} stored leaf cells scanned → {o.width}×{o.height} display pixels. Coordinate-overlap-weighted display means; not native values or scientific integrals. Units remain unknown.</p>
  <p>Click to locate the exact native cell at stored x1[/x2] coordinates. Keyboard pixel selection reads its largest-overlap representative. Inspector shows the raw stored cell, not the LOD mean. Zoom/pan redraw this existing LOD; zoom does not fetch finer data.</p>
  {leaves&&<fieldset><legend>Native leaf block outlines · same file digest</legend>
   <label><input type="checkbox" checked={showBlocks} onChange={e=>setShowBlocks(e.target.checked)}/>Show native leaf outlines</label>
   {levels.map(level=><label key={level}><input type="checkbox" checked={!hiddenLevels.includes(level)}
    onChange={e=>setHiddenLevels(v=>e.target.checked?v.filter(n=>n!==level):[...v,level])}/>Level {level}</label>)}
   <p>{leaves.complete?'Complete stored leaf set':'Limited outline set'}: {leaves.blocks.length} / {leaves.totalBlocks} blocks · cap {leaves.limit}.
    Level filters change outlines only; the field LOD still contains all scanned leaves. No parent/coarse blocks are synthesized.</p>
  </fieldset>}
  <button onClick={()=>{onViewChange?.();setView(v=>zoomNativeSpatialView(v,o.dimension,.8));}}>Zoom in · LOD</button>
  <button onClick={()=>{onViewChange?.();setView(v=>zoomNativeSpatialView(v,o.dimension,1.25));}}>Zoom out · LOD</button>
  <button onClick={()=>{onViewChange?.();setView(fullView);onFitFull?.();}}>Fit full domain · LOD</button>
  {onRefine&&<button disabled={disabled} onClick={()=>onRefine({x:[...view.x],y:o.dimension===2?[...view.y]:[0,1]})}>Read finer current viewport · scans leaves</button>}
  {o.values.some(v=>v===null)&&<p role="alert">Magenta pixels / 1D gaps mean empty, nonfinite or overflowed display reduction; raw Inspector remains available.</p>}
  <svg ref={svgRef} viewBox="0 0 900 370" preserveAspectRatio="none" aria-label="Global Plotfile LOD plot"
   style={{width:'100%',height:370,touchAction:'none',background:'#14232c'}}
   onPointerDown={down} onPointerMove={move} onPointerUp={up}
   onPointerCancel={()=>{const d=drag.current;drag.current=null;if(d){onViewChange?.();setView(d.view);}}}>
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
   {leaves&&showBlocks&&<g clipPath={'url(#'+clipId+')'} pointerEvents="none" aria-label="Native AMR leaf outlines">
    {leaves.blocks.filter(b=>!hiddenLevels.includes(b.level)).map(b=><rect key={b.index}
     data-native-block={b.index} data-native-level={b.level} x={X(b.lower[0])}
     y={o.dimension===2?Y(b.upper[1]):frame.top}
     width={(b.upper[0]-b.lower[0])/(view.x[1]-view.x[0])*frame.width}
     height={o.dimension===2?(b.upper[1]-b.lower[1])/(view.y[1]-view.y[0])*frame.height:frame.height}
     fill="none" stroke={selectedBlock===b.index?'#fff':'hsl('+((b.level*67)%360)+',90%,70%)'} strokeWidth={selectedBlock===b.index?3:1.2}/>
    )}
   </g>}
   {[0,.25,.5,.75,1].map(f=><g key={f} fill="#d5e2eb" fontSize="11">
    <text x={frame.left+f*frame.width} y={335} textAnchor="middle">{plotValue(f,view.x).toPrecision(4)}</text>
    <text x={65} y={frame.top+(1-f)*frame.height+4} textAnchor="end">{plotValue(f,view.y).toPrecision(4)}</text>
   </g>)}
   <text x={460} y={363} textAnchor="middle" fill="#d5e2eb">x1 · unit unknown</text>
   <text x={16} y={165} transform="rotate(-90 16 165)" textAnchor="middle" fill="#d5e2eb">{o.dimension===2?'x2 · unit unknown':o.field+' · display mean'}</text>
  </svg>
  {o.dimension===2&&range&&<p>Viridis display means: {range[0]} → {range[1]} · unit unknown.</p>}
  {leaves&&<details><summary>Native leaf block Inspector</summary>
   <p>File-local identity · {samples.audit.file.sha256}. Bounds span the stored native cells; these are leaf records, not a full parent hierarchy.</p>
   <div className="audit-table-scroll"><table><thead><tr><th>Inspect block</th><th>Stored index</th><th>Level</th><th>Logical key</th></tr></thead><tbody>
    {leaves.blocks.map(b=><tr key={b.index}><td><button aria-pressed={selectedBlock===b.index}
     onClick={()=>setSelectedBlock(b.index)}>Inspect native block {b.index}</button></td><td>{b.index}</td><td>{b.level}</td><td>{b.logicalKey}</td></tr>)}
   </tbody></table></div>
   {selectedLeaf&&<dl><dt>Native block / logical key / level</dt><dd>{selectedLeaf.index} / {selectedLeaf.logicalKey} / {selectedLeaf.level}</dd>
    <dt>Native bounds x1 / x2 / x3 · units unknown</dt><dd>{selectedLeaf.lower.map((lo,i)=>'['+lo+', '+selectedLeaf.upper[i]+']').join(' / ')}</dd>
    <dt>No-ghost cell shape · x1 / x2 / x3</dt><dd>{selectedLeaf.cellShape.join(' / ')}</dd>
    <dt>First stored native cell index</dt><dd>{selectedLeaf.firstCellIndex}</dd>
    <dt>Identity scope</dt><dd>Only this file digest; no cross-run block identity claim.</dd>
   </dl>}
  </details>}
  <p>{leaves?'Stored leaf outlines available; scientific/units review remains pending.':'Native leaf records unavailable in this response.'} {o.diagnostics.join(' · ')}</p>
 </section>;
}
