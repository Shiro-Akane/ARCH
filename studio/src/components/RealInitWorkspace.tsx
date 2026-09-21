import {previewCoordinateDomain} from '../data/plotPresentation';
import {pairingSuspicion,previewMetadataMatches} from '../data/configurationIdentity';
import {useCoreParameters} from '../state/coreParameters';
import {parsePar,effectiveEntries} from '../data/ParDocument';
import {useEffect,useLayoutEffect,useMemo,useRef,useState} from 'react';
import {useHost} from '../host/hostContext';
import {RealInitPreviewProvider,canAcceptRevision,realInitLine,realInitGrid,gridPoint} from '../data/RealInitPreviewProvider';
import type {WorkingCopy} from '../data/RealInitPreviewProvider';
import type {PreviewStatus,RealPreviewResult} from '../host/previewContracts';
import {nearestSample} from '../data/LinePreviewData';
import {RealGridRenderer} from './Preview/RealGridRenderer';
import {RealLineRenderer as LineRenderer} from './Preview/RealLineRenderer';
import {ParameterInspector} from './Inspector/ParameterInspector';
import type {ParameterDetails} from './Inspector/ParameterInspector';
export function RealInitWorkspace({copy,active,parameter,onSample}:{copy:WorkingCopy|null;active:boolean;parameter:ParameterDetails|null;onSample:()=>void}){
 const coreParameters=useCoreParameters();
 const host=useHost();const projectId=host.connected?host.snapshot?.session.projectId??'':'';
 const provider=useRef(new RealInitPreviewProvider());const request=useRef<string|null>(null);const cancelled=useRef(false);const alive=useRef(true);
 const profileId=coreParameters.model==='Sod'?'sod-initial-cpu':'cellular-initial-cpu';const [previewConfirmation,setPreviewConfirmation]=useState<{text:string;model:string;projectId:string}|null>(null);const [nx,setNx]=useState(128);const [ny,setNy]=useState(128);const selectionKey=profileId+':'+nx+':'+ny;
 const latestSelection=useRef(selectionKey);useLayoutEffect(()=>{latestSelection.current=selectionKey;},[selectionKey]);
 const latest=useRef({text:'',projectId:'',valid:false});
 useLayoutEffect(()=>{latest.current={text:copy?.text??'',projectId,valid:copy?.valid??false};},[copy,projectId]);
 const [ownedRequestId,setOwnedRequestId]=useState<string|null>(null);
 const [attemptFailed,setAttemptFailed]=useState(false);
 const [status,setStatus]=useState<PreviewStatus|null>(null);const [busy,setBusy]=useState(false);const [message,setMessage]=useState('Connect a Host with an authoritative Preview Profile.');
 const [saved,setSaved]=useState<{result:RealPreviewResult;text:string;selectionKey:string}|null>(null);const [field,setField]=useState('');const [selected,setSelected]=useState<number|null>(null);
 useEffect(()=>{alive.current=true;const api=provider.current;return()=>{alive.current=false;if(request.current)void api.cancel(request.current).catch(()=>{});};},[]);
 useEffect(()=>{if(!projectId)return;let disposed=false;let pending=false;const refresh=async()=>{if(pending)return;pending=true;try{const s=await provider.current.status(projectId);if(!disposed)setStatus(s);}catch(e){if(!disposed){setStatus(null);setMessage(e instanceof Error?e.message:'Preview unavailable');}}finally{pending=false;}};void refresh();const timer=setInterval(()=>void refresh(),1500);return()=>{disposed=true;clearInterval(timer);};},[projectId]);
 const currentStatus=status?.projectId===projectId?status:null;
 const buildId=currentStatus?.ready?currentStatus.build?.buildId:undefined;const binarySha256=currentStatus?.ready?currentStatus.build?.outputBinary.fingerprint.sha256:undefined;const setBuildScope=coreParameters.setBuildScope;
 useEffect(()=>{setBuildScope(projectId&&buildId&&binarySha256?{projectId,buildId,binarySha256}:null);},[projectId,buildId,binarySha256,setBuildScope]);
 const result=saved?.result;const fields=result?.core.data?.fields??[];
 const fieldKey=fields.some(f=>f.key===field)?field:fields[0]?.key??'';
 const line=useMemo(()=>result?.core.data?.dimension===1&&fieldKey?realInitLine(result,fieldKey):null,[result,fieldKey]);
 const grid=useMemo(()=>result?.core.data?.dimension===2&&fieldKey?realInitGrid(result,fieldKey):null,[result,fieldKey]);
 const chosen=currentStatus?.profiles?.find(p=>p.id===profileId)??currentStatus?.profile;
 const samplingValid=profileId!=='cellular-initial-cpu'||(Number.isInteger(nx)&&Number.isInteger(ny)&&nx>=2&&ny>=2&&nx<=256&&ny<=256);
 const staleConfig=!!saved&&(saved.selectionKey!==selectionKey||!copy?.valid||saved.text!==copy.text||result?.identity.projectId!==projectId);
 const staleBuild=!!result&&(!currentStatus?.ready||currentStatus.build?.buildId!==result.identity.buildId||currentStatus.build?.outputBinary.fingerprint.sha256!==result.identity.binarySha256);
 const current=!!saved&&!staleConfig&&!staleBuild&&!busy&&!attemptFailed&&currentStatus?.state==='succeeded'&&currentStatus.requestId===result?.identity.requestId;
 async function generate(confirmed=false){
  if(!samplingValid||busy||!copy?.valid||!projectId||!currentStatus?.ready)return;
  if(pairingSuspicion(coreParameters.model,copy.filename)&&!confirmed){setPreviewConfirmation({text:copy.text,model:coreParameters.model,projectId});return;}
  if(confirmed&&(!previewConfirmation||previewConfirmation.text!==copy.text||previewConfirmation.model!==coreParameters.model||previewConfirmation.projectId!==projectId)){setPreviewConfirmation(null);setMessage('Configuration changed; confirm pairing again.');return;}setPreviewConfirmation(null);
  const start={text:copy.text,projectId};const startedSelection=selectionKey;setBusy(true);setAttemptFailed(false);cancelled.current=false;setMessage('Generating real initial-condition samples…');
  try{
   const identity=await provider.current.start(projectId,start.text,profileId,profileId==='cellular-initial-cpu'?[ny,nx]:undefined);request.current=identity.requestId;setOwnedRequestId(identity.requestId);
   if(cancelled.current||!alive.current){await provider.current.cancel(identity.requestId);}
   const deadline=Date.now()+135000;
   while(Date.now()<deadline){
    const s=await provider.current.status(projectId);
    if(alive.current&&latest.current.projectId===projectId)setStatus(s);
    if(s.requestId!==identity.requestId)throw new Error('Preview request superseded; result discarded.');
    if(s.state==='generating'){await new Promise(r=>setTimeout(r,150));continue;}
    if(cancelled.current||s.state==='cancelled')throw new Error('Preview cancelled. Previous preview retained.');
    if(s.state!=='succeeded')throw new Error(s.error??'Preview failed.');
    const accepted=provider.current.accept(s.result,identity);
    if(!alive.current)return;
    if(startedSelection!==latestSelection.current||!canAcceptRevision(start,latest.current)){setAttemptFailed(true);setMessage('Preview result discarded: configuration or project changed while generating.');return;}
    setSaved({result:accepted,text:start.text,selectionKey:startedSelection});coreParameters.setSnapshot({result:accepted,text:start.text});setSelected(null);setMessage('Real Preview generated from the current Working Copy.');return;
   }
   throw new Error('Preview status timed out.');
  }catch(e){if(request.current)void provider.current.cancel(request.current).catch(()=>{});if(alive.current){setAttemptFailed(true);setMessage(e instanceof Error?e.message:'Preview failed; previous preview retained.');}}
  finally{request.current=null;if(alive.current)setBusy(false);}
 }
 async function cancel(){cancelled.current=true;setMessage('Cancelling Preview…');try{if(request.current)await provider.current.cancel(request.current);}catch(e){setMessage(e instanceof Error?e.message:'Cancel failed; waiting for owned process.');}}
 const binding=previewMetadataMatches(result,coreParameters.buildScope,coreParameters.model)&&result?.identity.caseId==='Sod'&&result.identity.projectId===projectId?result?.core.graphicalBindings?.items.find(b=>b.id==='Sod.x_pos'&&b.editable):undefined;
 const workingEntries=copy?effectiveEntries(parsePar(copy.text)):[];
 const token=workingEntries.find(e=>e.key===binding?.parameterKey)?.value;
 const markerValue=coreParameters.candidate??(saved?.text===copy?.text?binding?.coordinate:token===undefined?Number(result?.core.parameterMetadata?.parameters.find(m=>m.key===binding?.parameterKey)?.defaultValue??NaN):Number(token));
 const oldEntries=saved?effectiveEntries(parsePar(saved.text)):[];
 const boundsUnchanged=['x1min','x1max','xmin','xmax','x1_min','x1_max','geometry','nblockx1','nblockx2','nblockx3'].every(k=>workingEntries.find(e=>e.key===k)?.value===oldEntries.find(e=>e.key===k)?.value);
 const marker=binding&&copy&&boundsUnchanged&&!staleBuild?{binding,value:markerValue??NaN,pending:!current,onCandidate:(value:number|null)=>{coreParameters.setCandidate(value);if(value!==null)coreParameters.focus(binding.parameterKey);},onCommit:(value:number)=>coreParameters.edit(binding.parameterKey,String(value))}:undefined;
 const sample=selected!==null&&selected<(line?.values.length??grid?.values.length??0)?selected:null;
 return <><section hidden={!active} className="real-init-workspace" aria-label="Real initial condition preview">
  <div className="preview-heading"><div><span className="demo-badge">REAL IC</span><h1>Real initial condition</h1></div><span>{busy?'Generating':current?'Preview: Current':saved?`Previous preview · ${staleConfig?'parameters changed':staleBuild?'build changed or unverified':'retained'}`:'No real preview generated'}</span></div>
  <p>{chosen?.caseId??'Sod'} · CPU initialization · Working Copy{copy?.dirty?' (unsaved)':''}</p>
  <div className="real-preview-actions"><label>Model <select aria-label="Real IC model" value={profileId} onChange={e=>{coreParameters.setModel(e.target.value==='cellular-initial-cpu'?'CellularDet':'Sod');coreParameters.setCandidate(null);setSelected(null);}}>{(currentStatus?.profiles??(currentStatus?[currentStatus.profile]:[])).map(p=><option key={p.id} value={p.id}>{p.caseId} · {p.dimension}D</option>)}</select></label>{profileId==='cellular-initial-cpu'&&<><label>Nx <input aria-label="Samples x1" type="number" min={2} max={256} value={nx} onChange={e=>setNx(Number(e.target.value))}/></label><label>Ny <input aria-label="Samples x2" type="number" min={2} max={256} value={ny} onChange={e=>setNy(Number(e.target.value))}/></label></>}<button disabled={!samplingValid||busy||!copy?.valid||!currentStatus?.ready||!projectId} onClick={()=>void generate()}>{saved?'Update Preview':'Generate Real Preview'}</button><button disabled={!busy} onClick={()=>void cancel()}>Cancel Preview</button><label>Field <select aria-label="Real IC field" disabled={!fields.length} value={fieldKey} onChange={e=>{setField(e.target.value);setSelected(null);}}>{fields.map(f=><option key={f.key} value={f.key}>{f.displayName||f.key}</option>)}</select></label></div>
  <p role="status">{!current&&!busy&&message==='Real Preview generated from the current Working Copy.'?'Previous successful preview retained; update to validate this Working Copy.':message==='Connect a Host with an authoritative Preview Profile.'&&currentStatus?.ready?'Ready to generate from the current Working Copy.':message}</p><p className="section-note">{currentStatus?.reason??'No authoritative Preview Profile available. Connect the configured CPU integration project.'}</p>
  {previewConfirmation&&<div role="dialog" aria-label="Confirm model and configuration pairing" className="config-conflict"><p>Current Model: {coreParameters.model} · Parameter File: {copy?.filename}</p><p>{pairingSuspicion(coreParameters.model,copy?.filename??'')}</p><button onClick={()=>void generate(true)}>Continue Preview</button><button onClick={()=>{setPreviewConfirmation(null);document.querySelector<HTMLButtonElement>('[data-open-config]')?.click();}}>Switch Config</button><button onClick={()=>setPreviewConfirmation(null)}>Cancel</button></div>}
  {!copy&&<p>Open a configuration to prepare the Working Copy.</p>}{copy&&!copy.valid&&<p>Correct configuration validation issues before generating.</p>}
  {line&&<><LineRenderer domain={previewCoordinateDomain(result?.core.state,'x1')} marker={marker} data={line} selected={sample} onPoint={x=>{setSelected(nearestSample(line,x));onSample();}}/><p>{line.values.length} init-samples · min {line.min.toPrecision(6)} · max {line.max.toPrecision(6)} · {fields.find(f=>f.key===fieldKey)?.unit??'Unit not provided'}</p><label>Inspect sample <input aria-label="Real IC sample index" type="number" min={0} max={line.x.length-1} value={sample??''} onChange={e=>{const n=Number(e.target.value);if(e.target.value!==''&&Number.isInteger(n)&&n>=0&&n<line.x.length){setSelected(n);onSample();}}}/></label></>}
  {grid&&<><RealGridRenderer xDomain={previewCoordinateDomain(result?.core.state,'x1')} yDomain={previewCoordinateDomain(result?.core.state,'x2')} data={grid} selected={sample} onPoint={(x,y)=>{const point=gridPoint(grid,x,y);if(point){setSelected(point.index);onSample();}}}/><p>{result?.identity.caseId} · {grid.width} × {grid.height} init-samples · shape [{grid.height}, {grid.width}] · x1-fastest · x3 = 0</p><p>min {grid.min.toPrecision(6)} · max {grid.max.toPrecision(6)} · {grid.unit??'Unit not provided'}</p><label>Inspect sample <input aria-label="Real IC sample index" type="number" min={0} max={grid.values.length-1} value={sample??''} onChange={e=>{const n=Number(e.target.value);if(e.target.value!==''&&Number.isInteger(n)&&n>=0&&n<grid.values.length){setSelected(n);onSample();}}}/></label></>}
  {currentStatus?.failure&&currentStatus.requestId===ownedRequestId&&<details><summary>Latest failed request state · previous successful image retained</summary><pre>{JSON.stringify({identity:currentStatus.failure.identity,stage:currentStatus.failure.stage,state:currentStatus.failure.state},null,2)}</pre></details>}
  {binding&&!boundsUnchanged&&<p>Binding bounds need a new Preview after domain changes.</p>}
  {result&&<section aria-label="Preview state" className="preview-state"><h3>Preview state · {current?'Current':'Previous successful preview'}</h3><p>{previewStateSummary(result.core.state)}</p>{['grid','eos','species','amr'].map(key=><details key={key}><summary>{key==='grid'?'Region / grid':key==='eos'?'EOS loading':key==='species'?'Species':'AMR configuration · no hierarchy'}</summary><pre>{JSON.stringify(result.core.state?.[key]??'Unavailable',null,2)}</pre></details>)}</section>}
  {result&&<details className="preview-provenance"><summary>Preview provenance · {result.identity.caseId} · Build {result.identity.buildId.slice(0,8)}</summary><dl>{Object.entries(result.identity).map(([k,v])=><div key={k}><dt>{k}</dt><dd>{v}</dd></div>)}<dt>Generated</dt><dd>{result.generatedAt}</dd><dt>Sampling</dt><dd>Uniform bin-center · init-sample · not simulation cells</dd><dt>Metadata</dt><dd>{result.core.parameterMetadata?'Core metadata · partial coverage':'Parameter metadata unavailable'}</dd></dl></details>}
  {((currentStatus?.requestId===ownedRequestId&&currentStatus?.diagnostics?.length)||result?.core.diagnostics.length)?<details><summary>Core diagnostics</summary><ul>{((currentStatus?.requestId===ownedRequestId?currentStatus?.diagnostics:undefined)??result?.core.diagnostics??[]).map((d,i)=><li key={i}>{d.severity} · {d.code}: {d.message}</li>)}</ul></details>:null}
 </section><div hidden={!active} className="real-init-inspector">{parameter?<ParameterInspector parameter={parameter} metadata={previewMetadataMatches(result,coreParameters.buildScope,coreParameters.model)&&saved?.text===copy?.text?result?.core.parameterMetadata?.parameters.find(m=>m.key===parameter.key):undefined} revision={previewMetadataMatches(result,coreParameters.buildScope,coreParameters.model)&&saved?.text===copy?.text?result?.identity.configRevision:undefined} candidate={coreParameters.candidate}/>:<aside className="inspector panel" aria-label="Real IC Inspector"><div className="panel-heading"><h2>Sample Inspector</h2></div><div className="inspector-content"><p>Source: ARCH initialization</p>{sample!==null&&(line||grid)&&result?.core.data?<><p>{result.core.data.axes[0].name}: {(line?line.x[sample]:grid!.x[sample%grid!.width]).toPrecision(8)}</p><p>{result.core.data.axes[0].unit??'Unit not provided'}</p>{grid&&<><p>x2: {grid.y[Math.floor(sample/grid.width)].toPrecision(8)}</p><p>i={sample%grid.width}, j={Math.floor(sample/grid.width)} · index={sample} (j*Nx+i)</p><p>x3: 0 · {result.core.data.sampling.fixedCoordinates?.[0]?.unit??'Unit not provided'}</p></>}<dl>{fields.map(f=><div key={f.key}><dt>{f.displayName||f.key}</dt><dd>{f.values[sample].toPrecision(8)} · {f.unit??'Unit not provided'}</dd></div>)}</dl><p>{result.identity.caseId} · init-sample {sample}</p><p>Build {result.identity.buildId.slice(0,8)}</p><p>Config {result.identity.configRevision.slice(0,12)}</p><p>Binary {result.identity.binarySha256.slice(0,12)}</p></>:<p>Click a real sample or select its index.</p>}</div></aside>}</div></>;
}

function previewStateSummary(state:Record<string,unknown>|null|undefined){
 const grid=state?.grid as {axes?:{name:string;min:number;max:number}[]}|undefined;
 const eos=state?.eos as {status?:string;resolved?:string}|undefined;
 const species=state?.species as {name:string}[]|undefined;
 const amr=state?.amr as {minLevel?:number;maxLevel?:number}|undefined;
 return `Region: ${grid?.axes?.map(a=>a.name+' ['+a.min+', '+a.max+']').join('; ')??'Unavailable'} · EOS: ${eos?.resolved??'Unknown'} / ${eos?.status??'Unknown'} · Species: ${species?.map(s=>s.name).join(', ')??'Unavailable'} · AMR config: ${amr?.minLevel??'?'}–${amr?.maxLevel??'?'}; hierarchy not constructed`;
}
