import {useEffect,useLayoutEffect,useMemo,useRef,useState} from 'react';
import {useHost} from '../host/hostContext';
import {RealInitPreviewProvider,canAcceptRevision,realInitLine} from '../data/RealInitPreviewProvider';
import type {WorkingCopy} from '../data/RealInitPreviewProvider';
import type {PreviewStatus,RealPreviewResult} from '../host/previewContracts';
import {nearestSample} from '../data/LinePreviewData';
import {LineRenderer} from './Preview/LineRenderer';
import {ParameterInspector} from './Inspector/ParameterInspector';
import type {ParameterDetails} from './Inspector/ParameterInspector';
export function RealInitWorkspace({copy,active,parameter,onSample}:{copy:WorkingCopy|null;active:boolean;parameter:ParameterDetails|null;onSample:()=>void}){
 const host=useHost();const projectId=host.connected?host.snapshot?.session.projectId??'':'';
 const provider=useRef(new RealInitPreviewProvider());const request=useRef<string|null>(null);const cancelled=useRef(false);const alive=useRef(true);
 const latest=useRef({text:'',projectId:'',valid:false});
 useLayoutEffect(()=>{latest.current={text:copy?.text??'',projectId,valid:copy?.valid??false};},[copy,projectId]);
 const [ownedRequestId,setOwnedRequestId]=useState<string|null>(null);
 const [attemptFailed,setAttemptFailed]=useState(false);
 const [status,setStatus]=useState<PreviewStatus|null>(null);const [busy,setBusy]=useState(false);const [message,setMessage]=useState('Connect a Host with an authoritative Preview Profile.');
 const [saved,setSaved]=useState<{result:RealPreviewResult;text:string}|null>(null);const [field,setField]=useState('');const [selected,setSelected]=useState<number|null>(null);
 useEffect(()=>{alive.current=true;const api=provider.current;return()=>{alive.current=false;if(request.current)void api.cancel(request.current).catch(()=>{});};},[]);
 useEffect(()=>{if(!projectId)return;let disposed=false;let pending=false;const refresh=async()=>{if(pending)return;pending=true;try{const s=await provider.current.status(projectId);if(!disposed)setStatus(s);}catch(e){if(!disposed){setStatus(null);setMessage(e instanceof Error?e.message:'Preview unavailable');}}finally{pending=false;}};void refresh();const timer=setInterval(()=>void refresh(),1500);return()=>{disposed=true;clearInterval(timer);};},[projectId]);
 const currentStatus=status?.projectId===projectId?status:null;
 const result=saved?.result;const fields=result?.core.data?.fields??[];
 const fieldKey=fields.some(f=>f.key===field)?field:fields[0]?.key??'';
 const line=useMemo(()=>result&&fieldKey?realInitLine(result,fieldKey):null,[result,fieldKey]);
 const staleConfig=!!saved&&(!copy?.valid||saved.text!==copy.text||result?.identity.projectId!==projectId);
 const staleBuild=!!result&&(!currentStatus?.ready||currentStatus.build?.buildId!==result.identity.buildId||currentStatus.build?.outputBinary.fingerprint.sha256!==result.identity.binarySha256);
 const current=!!saved&&!staleConfig&&!staleBuild&&!busy&&!attemptFailed&&currentStatus?.state==='succeeded'&&currentStatus.requestId===result?.identity.requestId;
 async function generate(){
  if(busy||!copy?.valid||!projectId||!currentStatus?.ready)return;
  const start={text:copy.text,projectId};setBusy(true);setAttemptFailed(false);cancelled.current=false;setMessage('Generating real initial-condition samples…');
  try{
   const identity=await provider.current.start(projectId,start.text);request.current=identity.requestId;setOwnedRequestId(identity.requestId);
   if(cancelled.current||!alive.current){await provider.current.cancel(identity.requestId);}
   const deadline=Date.now()+45000;
   while(Date.now()<deadline){
    const s=await provider.current.status(projectId);
    if(alive.current&&latest.current.projectId===projectId)setStatus(s);
    if(s.requestId!==identity.requestId)throw new Error('Preview request superseded; result discarded.');
    if(s.state==='generating'){await new Promise(r=>setTimeout(r,150));continue;}
    if(cancelled.current||s.state==='cancelled')throw new Error('Preview cancelled. Previous preview retained.');
    if(s.state!=='succeeded')throw new Error(s.error??'Preview failed.');
    const accepted=provider.current.accept(s.result,identity);
    if(!alive.current)return;
    if(!canAcceptRevision(start,latest.current)){setAttemptFailed(true);setMessage('Preview result discarded: configuration or project changed while generating.');return;}
    setSaved({result:accepted,text:start.text});setSelected(null);setMessage('Real Preview generated from the current Working Copy.');return;
   }
   throw new Error('Preview status timed out.');
  }catch(e){if(request.current)void provider.current.cancel(request.current).catch(()=>{});if(alive.current){setAttemptFailed(true);setMessage(e instanceof Error?e.message:'Preview failed; previous preview retained.');}}
  finally{request.current=null;if(alive.current)setBusy(false);}
 }
 async function cancel(){cancelled.current=true;setMessage('Cancelling Preview…');try{if(request.current)await provider.current.cancel(request.current);}catch(e){setMessage(e instanceof Error?e.message:'Cancel failed; waiting for owned process.');}}
 const sample=selected!==null&&line&&selected<line.x.length?selected:null;
 return <><section hidden={!active} className="real-init-workspace" aria-label="Real initial condition preview">
  <div className="preview-heading"><div><span className="demo-badge">REAL IC</span><h1>Real initial condition</h1></div><span>{busy?'Generating':current?'Preview: Current':saved?`Previous preview · ${staleConfig?'parameters changed':staleBuild?'build changed or unverified':'retained'}`:'No real preview generated'}</span></div>
  <p>Sod · CPU initialization · Working Copy{copy?.dirty?' (unsaved)':''}</p>
  <div className="real-preview-actions"><button disabled={busy||!copy?.valid||!currentStatus?.ready||!projectId} onClick={()=>void generate()}>{saved?'Update Preview':'Generate Real Preview'}</button><button disabled={!busy} onClick={()=>void cancel()}>Cancel Preview</button><label>Field <select aria-label="Real IC field" disabled={!fields.length} value={fieldKey} onChange={e=>{setField(e.target.value);setSelected(null);}}>{fields.map(f=><option key={f.key} value={f.key}>{f.displayName||f.key}</option>)}</select></label></div>
  <p role="status">{message==='Connect a Host with an authoritative Preview Profile.'&&currentStatus?.ready?'Ready to generate from the current Working Copy.':message}</p><p className="section-note">{currentStatus?.reason??'No authoritative Preview Profile available. Connect the configured CPU integration project.'}</p>
  {!copy&&<p>Open a configuration to prepare the Working Copy.</p>}{copy&&!copy.valid&&<p>Correct configuration validation issues before generating.</p>}
  {line&&<><LineRenderer data={line} selected={sample} onPoint={x=>{setSelected(nearestSample(line,x));onSample();}}/><p>{line.values.length} init-samples · min {line.min.toPrecision(6)} · max {line.max.toPrecision(6)} · {fields.find(f=>f.key===fieldKey)?.unit??'Unit not provided'}</p><label>Inspect sample <input aria-label="Real IC sample index" type="number" min={0} max={line.x.length-1} value={sample??''} onChange={e=>{const n=Number(e.target.value);if(e.target.value!==''&&Number.isInteger(n)&&n>=0&&n<line.x.length){setSelected(n);onSample();}}}/></label></>}
  {result&&<details className="preview-provenance"><summary>Preview provenance · Sod · Build {result.identity.buildId.slice(0,8)}</summary><dl>{Object.entries(result.identity).map(([k,v])=><div key={k}><dt>{k}</dt><dd>{v}</dd></div>)}<dt>Generated</dt><dd>{result.generatedAt}</dd><dt>Sampling</dt><dd>Uniform bin-center · init-sample · not simulation cells</dd><dt>Metadata</dt><dd>Parameter metadata unavailable</dd></dl></details>}
  {((currentStatus?.requestId===ownedRequestId&&currentStatus?.diagnostics?.length)||result?.core.diagnostics.length)?<details><summary>Core diagnostics</summary><ul>{((currentStatus?.requestId===ownedRequestId?currentStatus?.diagnostics:undefined)??result?.core.diagnostics??[]).map((d,i)=><li key={i}>{d.severity} · {d.code}: {d.message}</li>)}</ul></details>:null}
 </section><div hidden={!active} className="real-init-inspector">{parameter?<ParameterInspector parameter={parameter}/>:<aside className="inspector panel" aria-label="Real IC Inspector"><div className="panel-heading"><h2>Sample Inspector</h2></div><div className="inspector-content"><p>Source: ARCH initialization</p>{sample!==null&&line&&result?.core.data?<><p>{result.core.data.axes[0].name}: {line.x[sample].toPrecision(8)}</p><p>{result.core.data.axes[0].unit??'Unit not provided'}</p><dl>{fields.map(f=><div key={f.key}><dt>{f.displayName||f.key}</dt><dd>{f.values[sample].toPrecision(8)} · {f.unit??'Unit not provided'}</dd></div>)}</dl><p>Sod · init-sample {sample}</p><p>Build {result.identity.buildId.slice(0,8)}</p><p>Config {result.identity.configRevision.slice(0,12)}</p><p>Binary {result.identity.binarySha256.slice(0,12)}</p></>:<p>Click a curve sample or select its index.</p>}</div></aside>}</div></>;
}
