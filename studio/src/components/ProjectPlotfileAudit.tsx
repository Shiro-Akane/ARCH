import './ProjectPlotfileAudit.css';
import type {PlotfilePointRequest} from '../host/plotfilePoint';
import {PlotfileOverviewView} from './PlotfileOverviewView';
import type {PlotfileOverviewRequest} from '../host/plotfileOverview';
import {PlotfileNativeView} from './PlotfileNativeView';
import {PlotfileSourceEvidence} from './PlotfileSourceEvidence';
import {useEffect,useRef,useState} from 'react';
import {PlotfileNativeInspector} from './PlotfileNativeInspector';
import {useHost} from '../host/hostContext';
import {requestPlotfileAudit,requestPlotfileOverview,requestPlotfilePoint,storedCellIndices} from '../host/plotfileAudit';
import type {AuditResponse,SliceSelection} from '../host/plotfileAudit';

export function ProjectPlotfileAudit(){
 const host=useHost();
 return <section className="project-plotfile-audit" aria-label="Project Plotfile audit"><h2>Project file · read-only audit</h2>
  {host.connected&&host.snapshot?<ConnectedAudit key={host.snapshot.session.projectId} projectId={host.snapshot.session.projectId}/>:<p>Connect a Local Host to read a plotfile inside its managed project.</p>}
 </section>;
}
function ConnectedAudit({projectId}:{projectId:string}){
 const [path,setPath]=useState(''),[info,setInfo]=useState<AuditResponse|null>(null),[samples,setSamples]=useState<AuditResponse|null>(null);
 const [field,setField]=useState(''),[block,setBlock]=useState('0'),[start,setStart]=useState<string[]>([]),[count,setCount]=useState<string[]>([]);
 const [overview,setOverview]=useState<AuditResponse|null>(null),[refined,setRefined]=useState<AuditResponse|null>(null);
 const viewportRevision=useRef(0);
 const [selectedRow,setSelectedRow]=useState<number|null>(null);
 const [busy,setBusy]=useState(false),[message,setMessage]=useState('Enter a project-relative .h5 path and read metadata.');
 const sequence=useRef(0),active=useRef<AbortController|null>(null);
 useEffect(()=>()=>{sequence.current++;active.current?.abort();},[]);
 function cancel(){sequence.current++;active.current?.abort();active.current=null;setBusy(false);setMessage('Read cancelled; previous successful data retained.');}
 async function read(selection?:SliceSelection,overviewRequest?:PlotfileOverviewRequest,pointRequest?:PlotfilePointRequest){
  const requestedViewportRevision=viewportRevision.current;
  const relativePath=selection||overviewRequest||pointRequest?info?.relativePath:path;
  if(!relativePath)return;
  active.current?.abort();const controller=new AbortController();active.current=controller;
  const request=++sequence.current;setBusy(true);setMessage(pointRequest?'Locating exact native cell from stored bounds…':overviewRequest?'Scanning leaf cells for candidate display LOD…':selection?'Reading bounded raw samples…':'Reading file metadata…');
  try{
   const result=pointRequest?await requestPlotfilePoint(projectId,relativePath,controller.signal,pointRequest,info!.audit.file.sha256):overviewRequest?await requestPlotfileOverview(projectId,relativePath,controller.signal,overviewRequest,info!.audit.file.sha256):await requestPlotfileAudit(projectId,relativePath,controller.signal,selection,selection?info?.audit.file.sha256:undefined);
   if(request!==sequence.current)return;
   if(overviewRequest?.viewport&&requestedViewportRevision!==viewportRevision.current){setMessage('Viewport changed during read; result discarded, previous display retained.');return;}
   if(overviewRequest){
    if(overviewRequest.viewport)setRefined(result);else{setRefined(null);setOverview(result);}
    setMessage(overviewRequest.viewport?'Candidate viewport LOD loaded; full-domain display retained for Fit.':'Candidate global display LOD loaded; Inspector reads native cells separately.');
   }
   else if(selection||pointRequest){setSamples(result);setSelectedRow(0);setMessage('Raw samples loaded · completion and scientific identity remain unverified.');}
   else{setRefined(null);setOverview(null);setInfo(result);setSamples(null);setSelectedRow(null);setField(result.audit.fields[0].name);setBlock('0');setStart(result.audit.cellShape.map(()=> '0'));setCount(result.audit.cellShape.map((n,i)=>String(i===result.audit.cellShape.length-1?Math.min(8,n):1)));setMessage('Metadata loaded · select a bounded sample region.');}
  }catch(error){if(request===sequence.current)setMessage(error instanceof Error?error.message:'Read failed. Previous successful data retained.');}
  finally{if(request===sequence.current){setBusy(false);active.current=null;}}
 }
 function readSamples(){
  if(!info)return;
  const text=[block,...start,...count];
  if(text.some(s=>!/^\d+$/.test(s)||!Number.isSafeInteger(Number(s)))){setMessage('Enter explicit non-negative integer indices and positive counts.');return;}
  const selection={field,block:Number(block),start:start.map(Number),count:count.map(Number)};
  if(selection.block>=info.audit.blocks||selection.count.some((n,i)=>n<1||selection.start[i]+n>info.audit.cellShape[i])||selection.count.reduce((a,b)=>a*b,1)>512){setMessage('Selection must stay inside one block and contain at most 512 samples.');return;}
  void read(selection);
 }
 const payload=samples?.audit.payload;
 return <div className="plotfile-audit">
  <label>Project-relative file <input aria-label="Project plotfile path" value={path} onChange={e=>setPath(e.target.value)} placeholder="results/plt_0000.h5"/></label>
  <button disabled={busy||!path.trim()} onClick={()=>void read()}>Read metadata</button>
  <button disabled={!busy} onClick={cancel}>Cancel read</button>
  <p role="status">{message}</p>
  <p>File completion: unverified · Units: recorded declarations when available, review pending · Scientific provenance: unavailable. Raw inspection does not certify a simulation result.</p>
  {info&&<><dl><dt>Observed file</dt><dd>{info.relativePath}</dd><dt>SHA-256</dt><dd className="audit-digest">{info.audit.file.sha256}</dd><dt>Time</dt><dd>{info.audit.time} · {info.audit.timeUnit??'unit unknown'}</dd><dt>Geometry / dimension</dt><dd>{info.audit.geometry} / {info.audit.dimension}D</dd><dt>Stored shape</dt><dd>{info.audit.blocks} blocks × [{info.audit.cellShape.join(', ')}] · x1-fastest</dd></dl>
   <PlotfileSourceEvidence evidence={info.audit.candidateSourceIdentity}/>
   <label>Stored field <select aria-label="Audit field" value={field} onChange={e=>setField(e.target.value)}>{info.audit.fields.map(f=><option key={f.name} value={f.name}>{f.name}</option>)}</select></label>
   <label>Block index <input aria-label="Audit block" value={block} onChange={e=>setBlock(e.target.value)} inputMode="numeric"/></label>
   {info.audit.cellShape.map((n,i)=><fieldset key={i}><legend>Stored axis x{info.audit.dimension-i} · {n} cells</legend><label>Start <input aria-label={'Audit start '+i} value={start[i]} inputMode="numeric" onChange={e=>setStart(v=>v.map((s,j)=>j===i?e.target.value:s))}/></label><label>Count <input aria-label={'Audit count '+i} value={count[i]} inputMode="numeric" onChange={e=>setCount(v=>v.map((s,j)=>j===i?e.target.value:s))}/></label></fieldset>)}
   <button disabled={busy||!field} onClick={readSamples}>Read raw samples</button>
   <button disabled={busy||!field||!info.audit.candidateNativeGrid} onClick={()=>void read(undefined,{field,width:32,height:info.audit.dimension===1?1:24})}>Read global display LOD · scans leaves</button>
  </>}
  {overview&&<PlotfileOverviewView samples={refined??overview} fullSamples={overview} disabled={busy}
   onViewChange={()=>{viewportRevision.current++;}} onFitFull={()=>setRefined(null)}
   onRefine={viewport=>void read(undefined,{field:overview.audit.overview!.field,width:32,height:overview.audit.dimension===1?1:24,viewport})} onPoint={point=>void read(undefined,undefined,{field:overview.audit.overview!.field,point})} onInspect={index=>{
   const a=overview.audit,p=a.overview!;const perBlock=a.cellShape.reduce((x,y)=>x*y,1),block=Math.floor(index/perBlock);
   const ijk=storedCellIndices(a,block,index);
   void read({field:p.field,block,start:ijk.slice(0,a.dimension).reverse(),count:a.cellShape.map(()=>1)});
  }}/>}
  {payload&&<><h3>Displayed raw samples · {payload.field}</h3><p>{samples?.relativePath} · block {payload.block} · start [{payload.start.join(', ')}] · shape [{payload.shape.join(', ')}]. These are stored Cartesian centers. {payload.nativeCells?'Candidate native bounds and measure are available in the Inspector.':'Native cell bounds and measure were not recorded.'}</p>
   {payload.diagnostics.length>0&&<p role="alert">{payload.diagnostics.join(' · ')}</p>}
   {samples&&<PlotfileNativeView samples={samples} selectedRow={selectedRow} onSelect={setSelectedRow}/>}
   <div className="audit-table-scroll"><table><thead><tr><th>Inspect</th><th>Global index</th><th>Raw value · {payload.unit??'unit unknown'}</th><th>Stored x</th><th>Stored y</th><th>Stored z</th></tr></thead><tbody>{payload.values.map((v,i)=><tr key={payload.linearIndices[i]}><td><button aria-label={"Inspect stored cell "+payload.linearIndices[i]} aria-pressed={selectedRow===i} onClick={()=>setSelectedRow(i)}>Inspect</button></td><td>{payload.linearIndices[i]}</td><td>{String(v)}</td><td>{String(payload.coordinates.x[i])}</td><td>{String(payload.coordinates.y[i])}</td><td>{String(payload.coordinates.z[i])}</td></tr>)}</tbody></table></div>
   {samples&&selectedRow!==null&&<PlotfileNativeInspector samples={samples} row={selectedRow}/>}
  </>}
 </div>;
}
