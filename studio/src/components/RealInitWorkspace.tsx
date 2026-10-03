import {previewProfileForConfiguration} from '../data/previewProfileSelection';
import {useWorkflow} from '../state/workflowContext';
import {AmrControls} from './Workflow/AmrControls';
import {PhysicalPlot} from './Preview/PhysicalPlot';
import {matchingAmrField} from '../data/amrIdentity';
import {amrPlaneAxes,amrAxisLabel,amrAxisUnit,meshDomain} from '../data/amrGeometry';
import type {AmrMesh,WorkflowResult} from '../host/workflowContracts';
import {InitializationWorkflow} from './Workflow/InitializationWorkflow';
import {sameBuildScope} from '../host/configurationContracts';
import {previewInputIssue} from '../data/previewScheduling';
import {previewCoordinateDomain} from '../data/plotPresentation';
import {pairingSuspicion,previewMetadataMatches} from '../data/configurationIdentity';
import {useCoreParameters} from '../state/coreParameters';
import {parsePar,effectiveEntries} from '../data/ParDocument';
import {useCallback,useEffect,useLayoutEffect,useMemo,useRef,useState} from 'react';
import {useHost} from '../host/hostContext';
import {RealInitPreviewProvider,canAcceptRevision,realInitLine,realInitGrid,realInitSlice,realSampleCoordinates,axisLabel,gridPoint} from '../data/RealInitPreviewProvider';
import type {WorkingCopy} from '../data/RealInitPreviewProvider';
import type {PreviewStatus,RealPreviewResult} from '../host/previewContracts';
import {nearestSample} from '../data/LinePreviewData';
import {RealGridRenderer} from './Preview/RealGridRenderer';
import {RealLineRenderer as LineRenderer} from './Preview/RealLineRenderer';
import {ParameterInspector} from './Inspector/ParameterInspector';
import type {ParameterDetails} from './Inspector/ParameterInspector';
export function RealInitWorkspace({copy,active,parameter,onSample}:{copy:WorkingCopy|null;active:boolean;parameter:ParameterDetails|null;onSample:()=>void}){
 const coreParameters=useCoreParameters();const {setPreview}=useWorkflow();
 const host=useHost();const projectId=host.connected?host.snapshot?.session.projectId??'':'';
 const submissions=useRef<Promise<unknown>>(Promise.resolve());
 const generation=useRef(0);const scheduledKey=useRef('');
 const provider=useRef(new RealInitPreviewProvider());const request=useRef<string|null>(null);const cancelled=useRef(false);const alive=useRef(true);
 const [status,setStatus]=useState<PreviewStatus|null>(null);
 const [workflowBusy,setWorkflowBusy]=useState(false);
 const discovery=sameBuildScope(coreParameters.discovery,coreParameters.buildScope)?coreParameters.discovery:null;
 const discovered=discovery?.cases.find(c=>c.caseId===coreParameters.model);
 const profile=discovered?.initialFieldPreview?previewProfileForConfiguration(status?.projectId===projectId?status.profiles??[]:[],
  coreParameters.inspection?.response??null,copy?.text,coreParameters.inspection?.text,projectId,
  coreParameters.model,coreParameters.buildScope?.buildId,coreParameters.buildScope?.binarySha256):undefined;
 const profileId=profile?.id??'';const [previewConfirmation,setPreviewConfirmation]=useState<{text:string;model:string;projectId:string}|null>(null);const [samplingDraft,setSamplingDraft]=useState<{profileId:string;axes:number[]}>({profileId:'',axes:[128,128,32]});
 const defaults=profile?.defaultShape?[...profile.defaultShape].reverse():[128,128,32];
 const [nx,ny,nz]=samplingDraft.profileId===profileId?samplingDraft.axes:[defaults[0],defaults[1]??128,defaults[2]??32];
 const setSamples=(axis:number,value:number)=>setSamplingDraft({profileId,axes:[nx,ny,nz].map((n,i)=>i===axis?value:n)});
 const [sliceAxis,setSliceAxis]=useState(2),[sliceIndex,setSliceIndex]=useState(0);
 const dimensions=profile?.dimension??0;
 const selectionKey=coreParameters.model+':'+profileId+':'+nx+':'+ny+':'+nz;
 const latestSelection=useRef(selectionKey);useLayoutEffect(()=>{latestSelection.current=selectionKey;},[selectionKey]);
 const latest=useRef({text:'',projectId:'',valid:false});
 useLayoutEffect(()=>{latest.current={text:copy?.text??'',projectId,valid:copy?.valid??false};},[copy,projectId]);
 const [ownedRequestId,setOwnedRequestId]=useState<string|null>(null);
 const [attemptFailed,setAttemptFailed]=useState(false);
 const [busy,setBusy]=useState(false);const [message,setMessage]=useState('Connect a Host with an authoritative Preview Profile.');
 const [saved,setSaved]=useState<{result:RealPreviewResult;text:string;selectionKey:string}|null>(null);const [field,setField]=useState('');const [selected,setSelected]=useState<number|null>(null);
 useEffect(()=>{alive.current=true;const api=provider.current;return()=>{alive.current=false;if(request.current)void api.cancel(request.current).catch(()=>{});};},[]);
 useEffect(()=>{if(!projectId)return;let disposed=false;let pending=false;const refresh=async()=>{if(pending)return;pending=true;try{const s=await provider.current.status(projectId);if(!disposed)setStatus(s);}catch(e){if(!disposed){setStatus(null);setMessage(e instanceof Error?e.message:'Preview unavailable');}}finally{pending=false;}};void refresh();const timer=setInterval(()=>void refresh(),1500);return()=>{disposed=true;clearInterval(timer);};},[projectId]);
 const currentStatus=status?.projectId===projectId?status:null;
 const selectedSha=host.snapshot?.session.executable?.sha256;
 const buildId=currentStatus?.ready?currentStatus.build?.buildId:selectedSha?'selected-binary:'+selectedSha:undefined;const binarySha256=currentStatus?.ready?currentStatus.build?.outputBinary.fingerprint.sha256:selectedSha;const setBuildScope=coreParameters.setBuildScope;
 useEffect(()=>{setBuildScope(projectId&&buildId&&binarySha256?{projectId,buildId,binarySha256}:null);},[projectId,buildId,binarySha256,setBuildScope]);
 const result=saved?.result;const fields=result?.core.data?.fields??[];
 const fieldKey=fields.some(f=>f.key===field)?field:fields[0]?.key??'';
 const data=result?.core.data;
 const uniformState=data?.coordinates?.representation==='uniform-state';
 const line=useMemo(()=>result?.core.data?.dimension===1&&fieldKey&&!uniformState?realInitLine(result,fieldKey):null,[result,fieldKey,uniformState]);
 const effectiveSlice=data?.dimension===3?Math.min(sliceIndex,data.axes[sliceAxis].values.length-1):0;
 const grid=useMemo(()=>result&&fieldKey?(result.core.data?.dimension===2?realInitGrid(result,fieldKey)
  :result.core.data?.dimension===3?realInitSlice(result,fieldKey,sliceAxis,effectiveSlice):null):null,[result,fieldKey,sliceAxis,effectiveSlice]);
 const shownAxes=grid?.slice?[0,1,2].filter(axis=>axis!==grid.slice!.axis):[0,1];
 const samplingShape=profile?.dimension===3?[nz,ny,nx]:[ny,nx];

 const samplingValid=!!profile&&(profile.dimension===1||samplingShape.every(n=>Number.isInteger(n)&&n>=2&&n<=(profile.maxPerAxis??0))&&samplingShape.reduce((a,b)=>a*b,1)<=profile.maxSampleCount);
 const staleConfig=!!saved&&(saved.selectionKey!==selectionKey||!copy?.valid||saved.text!==copy.text||result?.identity.projectId!==projectId);
 const staleBuild=!!result&&(!currentStatus?.ready||currentStatus.build?.buildId!==result.identity.buildId||currentStatus.build?.outputBinary.fingerprint.sha256!==result.identity.binarySha256);
 const current=!!saved&&!staleConfig&&!staleBuild&&!busy&&!attemptFailed&&currentStatus?.state==='succeeded'&&currentStatus.requestId===result?.identity.requestId;
 const [latency,setLatency]=useState<{totalWallMilliseconds:number;renderToCurrentMilliseconds?:number;hostQueueMilliseconds?:number;hostElapsedMilliseconds?:number;coreElapsedMilliseconds?:number;transportParseMilliseconds?:number}|null>(null);
 const automaticInputIssue=copy&&previewMetadataMatches(result,coreParameters.buildScope,coreParameters.model)?previewInputIssue(copy.text,result?.core.parameterMetadata?.parameters):undefined;
 const generate=useCallback(async(confirmed=false,automatic=false)=>{
  if(automatic&&automaticInputIssue)return;
  if(!profileId||workflowBusy||!samplingValid||!copy?.valid||!projectId||!currentStatus?.ready)return;
  if(pairingSuspicion(coreParameters.model,copy.filename)&&!confirmed){setPreviewConfirmation({text:copy.text,model:coreParameters.model,projectId});return;}
  if(confirmed&&(!previewConfirmation||previewConfirmation.text!==copy.text||previewConfirmation.model!==coreParameters.model||previewConfirmation.projectId!==projectId)){setPreviewConfirmation(null);setMessage('Configuration changed; confirm pairing again.');return;}setPreviewConfirmation(null);
  const ticket=++generation.current;const submittedAt=performance.now();
  const start={text:copy.text,projectId};const startedSelection=selectionKey;setBusy(true);setAttemptFailed(false);cancelled.current=false;setLatency(null);setMessage(saved?'Parameters changed · updating…':'Preparing preview…');
  try{
   let identity;
   // Capability negotiation or cancellation may still own the previous process.
   // Retry only that bounded busy state, never invalid config/build/protocol errors.
   for(let retry=0;;retry++){
    try{
     const submission=submissions.current.catch(()=>{}).then(()=>{
      if(ticket!==generation.current||!alive.current)throw new Error('Obsolete Preview submission.');
      return provider.current.start(projectId,start.text,profileId,dimensions===3?[nz,ny,nx]:dimensions===2?[ny,nx]:undefined,coreParameters.model);
     });
     submissions.current=submission;
     identity=await submission;break;
    }
    catch(error){
     if(retry>=20||!(error instanceof Error)||!error.message.includes('already generating'))throw error;
     await new Promise(resolve=>setTimeout(resolve,150));
     if(ticket!==generation.current||!alive.current)return;
    }
   }
   if(ticket!==generation.current||!alive.current){
    if(cancelled.current||!alive.current)await provider.current.cancel(identity.requestId).catch(()=>{});
    return;
   }
   request.current=identity.requestId;setOwnedRequestId(identity.requestId);
   if(cancelled.current||!alive.current){await provider.current.cancel(identity.requestId);}
   let deadline=Date.now()+735000;let becameActive=false;
   while(Date.now()<deadline){
    const s=await provider.current.status(projectId);
    if(ticket!==generation.current||!alive.current)return;
    if(alive.current&&latest.current.projectId===projectId)setStatus(s);
    if(s.queue?.pendingRequestId===identity.requestId){await new Promise(r=>setTimeout(r,150));continue;}
    if(s.requestId!==identity.requestId)throw new Error('Preview request superseded; result discarded.');
    if(!becameActive){becameActive=true;deadline=Date.now()+375000;}
    if(s.state==='generating'){setMessage((saved?'Parameters changed · updating':'Preparing preview')+(s.session?.stage?' · '+s.session.stage:'')+'…');await new Promise(r=>setTimeout(r,150));continue;}
    if(cancelled.current||s.state==='cancelled')throw new Error('Preview cancelled. Previous preview retained.');
    if(s.state!=='succeeded')throw new Error(s.error??'Preview failed.');
    const accepted=provider.current.accept(s.result,identity);
    if(!alive.current)return;
    if(startedSelection!==latestSelection.current||!canAcceptRevision(start,latest.current)){setAttemptFailed(true);setMessage('Preview result discarded: configuration or project changed while generating.');return;}
    const acceptedAt=performance.now();
    setSaved({result:accepted,text:start.text,selectionKey:startedSelection});coreParameters.setSnapshot({result:accepted,text:start.text});setSelected(previous=>previous!==null&&previous<(accepted.core.data?.sampling.count??0)?previous:null);
    setMessage('Real Preview generated from the current Working Copy.');
    requestAnimationFrame(()=>requestAnimationFrame(()=>{
     if(ticket===generation.current&&alive.current)setLatency({totalWallMilliseconds:performance.now()-submittedAt,renderToCurrentMilliseconds:performance.now()-acceptedAt,...s.timing});
    }));return;
   }
   throw new Error('Preview status timed out.');
  }catch(e){if(ticket!==generation.current)return;if(request.current)void provider.current.cancel(request.current).catch(()=>{});if(alive.current){setAttemptFailed(true);setMessage(e instanceof Error?e.message:'Preview failed; previous preview retained.');}}
  finally{if(ticket===generation.current){request.current=null;if(alive.current)setBusy(false);}}
 },[setPreviewConfirmation,workflowBusy,samplingValid,copy,projectId,currentStatus?.ready,coreParameters,previewConfirmation,selectionKey,saved,profileId,dimensions,ny,nx,nz,automaticInputIssue]);
 async function cancel(){generation.current++;cancelled.current=true;setMessage('Cancelling Preview…');try{if(request.current)await provider.current.cancel(request.current);setBusy(false);setAttemptFailed(true);request.current=null;setMessage('Preview cancelled. Previous preview retained.');}catch(e){setMessage(e instanceof Error?e.message:'Cancel failed; waiting for owned process.');}}
 const generateLatest=useRef<typeof generate|null>(null);useLayoutEffect(()=>{generateLatest.current=generate;});
 const editKey=JSON.stringify([projectId,profileId,copy?.text,nx,ny,nz]);
 useEffect(()=>{
  if(!profileId||workflowBusy||automaticInputIssue||!active||!samplingValid||!copy?.valid||!currentStatus?.ready||!projectId||scheduledKey.current===editKey)return;
  const timer=setTimeout(()=>{if(scheduledKey.current===editKey)return;scheduledKey.current=editKey;void generateLatest.current?.(false,true);},300);
  return()=>clearTimeout(timer);
 },[profileId,workflowBusy,active,samplingValid,copy?.valid,currentStatus?.ready,projectId,editKey,automaticInputIssue]);
 const previewEnabled=!!(active&&profileId&&!workflowBusy&&samplingValid&&copy?.valid&&currentStatus?.ready&&projectId&&!busy);
 const previewState=busy?'generating':current?'current':saved?'previous / stale':'not generated';
 const previewReason=!projectId?'Connect Local Host.':!copy?'Open a configuration.':!copy.valid?'Resolve validation errors.':!profileId?(discovered?.initialFieldPreview?'Waiting for the current inspected dimension and matching Build.':'Registered model has no supported field Preview.'):!samplingValid?'Correct sampling dimensions.':workflowBusy?'Initialization task active.':busy?'Preview active.':!currentStatus?.ready?(currentStatus?.reason??'A matching successful Build is required.'):'Generate from the current unsaved Working Copy; does not Save.';
 useEffect(()=>{setPreview({enabled:previewEnabled,state:previewState,reason:previewReason,generate:()=>{scheduledKey.current=editKey;void generateLatest.current?.();}});},[setPreview,previewEnabled,previewState,previewReason,editKey]);
 const binding=previewMetadataMatches(result,coreParameters.buildScope,coreParameters.model)&&result?.identity.caseId==='Sod'&&result.identity.projectId===projectId?result?.core.graphicalBindings?.items.find(b=>b.id==='Sod.x_pos'&&b.editable):undefined;
 const workingEntries=copy?effectiveEntries(parsePar(copy.text)):[];
 const token=workingEntries.find(e=>e.key===binding?.parameterKey)?.value;
 const markerValue=coreParameters.candidate??(saved?.text===copy?.text?binding?.coordinate:token===undefined?Number(result?.core.parameterMetadata?.parameters.find(m=>m.key===binding?.parameterKey)?.defaultValue??NaN):Number(token));
 const oldEntries=saved?effectiveEntries(parsePar(saved.text)):[];
 const boundsUnchanged=['x1min','x1max','xmin','xmax','x1_min','x1_max','geometry','nblockx1','nblockx2','nblockx3'].every(k=>workingEntries.find(e=>e.key===k)?.value===oldEntries.find(e=>e.key===k)?.value);
 const marker=binding&&copy&&boundsUnchanged&&!staleBuild?{binding,value:markerValue??NaN,pending:!current,onCandidate:(value:number|null)=>{coreParameters.setCandidate(value);if(value!==null)coreParameters.focus(binding.parameterKey);},onCommit:(value:number)=>coreParameters.edit(binding.parameterKey,String(value))}:undefined;
 const [meshResult,setMeshResult]=useState<WorkflowResult>();
 const [levels,setLevels]=useState<Set<number>>(new Set()),[cells,setCells]=useState(true),[selecting,setSelecting]=useState(false),[block,setBlock]=useState<string|null>(null);
 const acceptMesh=useCallback((value:WorkflowResult)=>{setMeshResult(value);const m=value.core.data as AmrMesh;setLevels(new Set(m.levelCounts.map(l=>l.level)));setBlock(null);},[]);
 const mesh=meshResult?.core.data as AmrMesh|undefined;
 const [meshSliceDraft,setMeshSliceDraft]=useState<{requestId:string;axis:number;coordinate:number}|null>(null);
 const matching=!!meshResult&&current&&matchingAmrField(meshResult,result);
 const meshCurrent=!!meshResult&&sameBuildScope(meshResult.identity,coreParameters.buildScope)&&meshResult.identity.caseId===coreParameters.model&&meshResult.identity.configRevision===coreParameters.inspection?.response.identity.configRevision&&coreParameters.inspection?.text===copy?.text;
 const defaultMeshSliceDomain=mesh?.dimension===3?meshDomain(mesh,2):undefined;
 const independentSlice=mesh?.dimension===3?(meshSliceDraft&&meshSliceDraft.requestId===meshResult?.identity.requestId?meshSliceDraft:
  {requestId:meshResult?.identity.requestId??'',axis:2,coordinate:defaultMeshSliceDomain?(defaultMeshSliceDomain[0]+defaultMeshSliceDomain[1])/2:0}):undefined;
 const overlaySlice=matching&&grid?.slice?{axis:grid.slice.axis,coordinate:grid.slice.coordinate}:independentSlice;
 const amr=mesh&&mesh.snapshot!=='none'?{mesh,levels,cells,selecting,selected:block,onSelect:setBlock,slice:overlaySlice}:undefined;
 const amrAxes=mesh?amrPlaneAxes(mesh,overlaySlice):[];
 const domainX=mesh&&amrAxes.length?meshDomain(mesh,amrAxes[0]):undefined,domainY=mesh&&amrAxes.length>1?meshDomain(mesh,amrAxes[1]):undefined;
 const independentSliceDomain=mesh&&independentSlice?meshDomain(mesh,independentSlice.axis):undefined;
 const overlayMatching=matching&&!uniformState;
 const sample=selected!==null&&selected<(data?.sampling.count??0)?selected:null;
 const gridSample=sample===null?null:grid?.globalIndices?Array.from(grid.globalIndices).indexOf(sample):sample;
 const rawCoordinates=result&&sample!==null?realSampleCoordinates(result,sample):[];
 return <><section hidden={!active} className="real-init-workspace" aria-label="Real initial condition preview">
  <div className="preview-heading"><div><span className="demo-badge">REAL IC</span><h1>Real initial condition</h1></div><span>{busy?(saved?'Parameters changed · updating…':'Preparing preview…'):current?'Preview: Current':saved?`Previous preview · ${staleConfig?'parameters changed':staleBuild?'build changed or unverified':'retained'}`:'No real preview generated'}</span></div>
  <p>{coreParameters.model} · CPU initialization · Working Copy{copy?.dirty?' (unsaved)':''}</p>
  <div className="real-preview-actions"><label>Model <select aria-label="Real IC model" value={coreParameters.model} disabled={!discovery||busy||workflowBusy} onChange={e=>{coreParameters.setModel(e.target.value);coreParameters.setCandidate(null);setSelected(null);}}>{!discovery&&<option value={coreParameters.model}>Awaiting binary registry</option>}{discovery?.cases.map(p=><option key={p.caseId} value={p.caseId}>{p.caseId}{p.initialFieldPreview?' · '+p.previewDimensions.join('/')+'D':' · inspection only'}</option>)}</select></label>{dimensions>1&&<><label>Nx <input aria-label="Samples x1" type="number" min={2} max={profile?.maxPerAxis} value={nx} onChange={e=>setSamples(0,Number(e.target.value))}/></label><label>Ny <input aria-label="Samples x2" type="number" min={2} max={profile?.maxPerAxis} value={ny} onChange={e=>setSamples(1,Number(e.target.value))}/></label>{dimensions===3&&<label>Nz <input aria-label="Samples x3" type="number" min={2} max={profile?.maxPerAxis} value={nz} onChange={e=>setSamples(2,Number(e.target.value))}/></label>}</>}<button disabled={!profileId||workflowBusy||!samplingValid||!copy?.valid||!currentStatus?.ready||!projectId} onClick={()=>{scheduledKey.current=editKey;void generate();}}>{saved?'Update Preview':'Generate Real Preview'}</button><button disabled={!busy} onClick={()=>void cancel()}>Cancel Preview</button><label>Field <select aria-label="Real IC field" disabled={!fields.length} value={fieldKey} onChange={e=>{setField(e.target.value);setSelected(null);}}>{fields.map(f=><option key={f.key} value={f.key}>{f.displayName||f.key}</option>)}</select></label></div>
  {!profileId&&discovered&&!discovered.initialFieldPreview&&<p>Full field Preview unavailable. This registered model supports initialization inspection below.</p>}
  {!profileId&&discovered?.initialFieldPreview&&<p>Waiting for the current configuration dimension and a matching runtime Preview Profile.</p>}
  {grid?.globalIndices&&sample!==null&&gridSample===-1&&<p>Selected raw sample lies outside this slice; Inspector retains its original values. Select a sample on this plane to show its marker.</p>}
  <p role="status">{!current&&!busy&&message==='Real Preview generated from the current Working Copy.'?'Previous successful preview retained; update to validate this Working Copy.':message==='Connect a Host with an authoritative Preview Profile.'&&currentStatus?.ready?'Ready to generate from the current Working Copy.':message}</p><p className="section-note">{currentStatus?.reason??'No authoritative Preview Profile available. Connect the configured CPU integration project.'}</p>
  {previewConfirmation&&<div role="dialog" aria-label="Confirm model and configuration pairing" className="config-conflict"><p>Current Model: {coreParameters.model} · Parameter File: {copy?.filename}</p><p>{pairingSuspicion(coreParameters.model,copy?.filename??'')}</p><button onClick={()=>void generate(true)}>Continue Preview</button><button onClick={()=>{setPreviewConfirmation(null);document.querySelector<HTMLButtonElement>('[data-open-config]')?.click();}}>Switch Config</button><button onClick={()=>setPreviewConfirmation(null)}>Cancel</button></div>}
  {automaticInputIssue&&<p role="status">Automatic preview paused: {automaticInputIssue} Previous successful preview retained.</p>}
  {!copy&&<p>Open a configuration to prepare the Working Copy.</p>}{copy&&!copy.valid&&<p>Correct configuration validation issues before generating.</p>}
  {line&&<><LineRenderer axisLabel={result?axisLabel(result,0)+(data?.axes[0].unit?' · '+data.axes[0].unit:''):undefined} amr={matching?amr:undefined} domain={previewCoordinateDomain(result?.core.state,'x1')} marker={marker} data={line} selected={sample} onPoint={x=>{setSelected(nearestSample(line,x));onSample();}}/><p>{line.values.length} init-samples · min {line.min.toPrecision(6)} · max {line.max.toPrecision(6)} · {fields.find(f=>f.key===fieldKey)?.unit??'Unit not provided'}</p><label>Inspect sample <input aria-label="Real IC sample index" type="number" min={0} max={line.x.length-1} value={sample??''} onChange={e=>{const n=Number(e.target.value);if(e.target.value!==''&&Number.isInteger(n)&&n>=0&&n<line.x.length){setSelected(n);onSample();}}}/></label></>}
  {uniformState&&<section aria-label="Uniform initial state"><h2>Uniform initial state</h2><p>Core reports a uniform-state representation. Values are initialized once; this is not a spatial profile or evolved burn trajectory.</p><dl>{fields.map(f=><div key={f.key}><dt>{f.displayName}</dt><dd>{f.values[0].toPrecision(8)} · {f.unit??'Unit not provided'}</dd></div>)}</dl><button onClick={()=>{setSelected(0);onSample();}}>Inspect initial state</button></section>}
  {data?.dimension===3&&<div className="real-preview-actions"><label>Fixed slice axis <select aria-label="Fixed slice axis" value={sliceAxis} onChange={e=>{setSliceAxis(Number(e.target.value));setSliceIndex(0);}}>{data.axes.map((a,i)=><option key={a.name} value={i}>{result?axisLabel(result,i):a.name}</option>)}</select></label><label>Slice sample <input aria-label="Slice sample index" type="number" min={0} max={data.axes[sliceAxis].values.length-1} value={effectiveSlice} onChange={e=>{const n=Number(e.target.value);if(Number.isInteger(n)&&n>=0&&n<data.axes[sliceAxis].values.length){setSliceIndex(n);}}}/></label><p>{result?axisLabel(result,sliceAxis):''} = {data.axes[sliceAxis].values[effectiveSlice].toPrecision(8)} · {data.axes[sliceAxis].unit??'Unit not provided'} · display only · full volume retained</p></div>}
  {grid&&<><RealGridRenderer amr={overlayMatching?amr:undefined} xDomain={previewCoordinateDomain(result?.core.state,data?.axes[shownAxes[0]].name??'x1')} yDomain={previewCoordinateDomain(result?.core.state,data?.axes[shownAxes[1]].name??'x2')} data={grid} selected={gridSample!==null&&gridSample!==undefined&&gridSample>=0?gridSample:null} onPoint={(x,y)=>{const point=gridPoint(grid,x,y);if(point){setSelected(grid.globalIndices?.[point.index]??point.index);onSample();}}}/><p>{result?.identity.caseId} · {grid.width} × {grid.height} init-samples · shape [{grid.height}, {grid.width}] · x1-fastest{grid.slice?' · volume slice':''}{data?.sampling.fixedCoordinates?.map(c=>' · '+c.name+' = '+c.value+' '+(c.unit??'')).join('')}</p><p>min {grid.min.toPrecision(6)} · max {grid.max.toPrecision(6)} · {grid.unit??'Unit not provided'}</p><label>Inspect sample <input aria-label="Real IC sample index" type="number" min={0} max={(data?.sampling.count??0)-1} value={sample??''} onChange={e=>{const n=Number(e.target.value);if(e.target.value!==''&&Number.isInteger(n)&&n>=0&&n<(data?.sampling.count??0)){setSelected(n);onSample();}}}/></label></>}
  {currentStatus?.failure&&currentStatus.requestId===ownedRequestId&&<details><summary>Latest failed request state · previous successful image retained</summary><pre>{JSON.stringify({identity:currentStatus.failure.identity,stage:currentStatus.failure.stage,state:currentStatus.failure.state},null,2)}</pre></details>}
  {binding&&!boundsUnchanged&&<p>Binding bounds need a new Preview after domain changes.</p>}
  <InitializationWorkflow copy={copy} fieldBusy={busy} buildReady={currentStatus?.ready===true} onBusy={setWorkflowBusy} onMesh={acceptMesh}/>
  {mesh&&amr&&<><p role="status">{overlayMatching?'AMR overlay · matching field/config/build/EOS/native coordinates identity':meshCurrent?'AMR shown separately: no matching field/config/build/EOS identity.':'Previous AMR shown separately · configuration/model/build changed.'}</p>{mesh.dimension===3&&!overlayMatching&&independentSlice&&<div className="real-preview-actions"><label>AMR fixed axis <select aria-label="AMR fixed slice axis" value={independentSlice.axis} onChange={e=>{const axis=Number(e.target.value),domain=meshDomain(mesh,axis);if(domain){setMeshSliceDraft({requestId:meshResult?.identity.requestId??'',axis,coordinate:(domain[0]+domain[1])/2});setBlock(null);}}}>{[0,1,2].map(a=><option key={a} value={a}>{amrAxisLabel(mesh,a)}</option>)}</select></label><label>AMR slice position <input aria-label="AMR slice native coordinate" type="number" step="any" min={independentSliceDomain?.[0]} max={independentSliceDomain?.[1]} value={independentSlice.coordinate} onChange={e=>{const coordinate=Number(e.target.value);if(e.target.value!==''&&Number.isFinite(coordinate)&&independentSliceDomain&&coordinate>=independentSliceDomain[0]&&coordinate<=independentSliceDomain[1]){setMeshSliceDraft({...independentSlice,coordinate});setBlock(null);}}}/></label><p>{amrAxisUnit(mesh,independentSlice.axis)??'Unit not provided'} · display only · actual intersecting leaves</p></div>}
 <AmrControls mesh={mesh} levels={levels} cells={cells} selecting={selecting} selected={block} onLevels={setLevels} onCells={setCells} onSelecting={setSelecting} onSelect={setBlock}/>{!overlayMatching&&domainX&&<PhysicalPlot xLabel={amrAxisLabel(mesh,amrAxes[0])+' · '+(amrAxisUnit(mesh,amrAxes[0])??'Unit not provided')} yLabel={amrAxes.length>1?amrAxisLabel(mesh,amrAxes[1])+' · '+(amrAxisUnit(mesh,amrAxes[1])??'Unit not provided'):undefined} meshOnly amr={amr} xDomain={domainX} yDomain={domainY} x={Float64Array.from(domainX)} y={domainY?Float64Array.from(domainY):undefined} values={new Float64Array()} field="Initial AMR hierarchy" selected={null} onPoint={()=>{}}/>}</>}
  {mesh?.snapshot==='none'&&<p>No current hierarchy snapshot. Previous field retained; no AMR overlay is attached.</p>}
  {result&&<section aria-label="Preview state" className="preview-state"><h3>Preview state · {current?'Current':'Previous successful preview'}</h3><p>{previewStateSummary(result.core.state)}</p>{['grid','eos','species','amr'].map(key=><details key={key}><summary>{key==='grid'?'Region / grid':key==='eos'?'EOS loading':key==='species'?'Species':'AMR configuration · no hierarchy'}</summary><pre>{JSON.stringify(result.core.state?.[key]??'Unavailable',null,2)}</pre></details>)}</section>}
  {currentStatus?.session&&<details><summary>Session diagnostics</summary><pre>{JSON.stringify({session:currentStatus.session,timing:latency},null,2)}</pre></details>}
  {result&&<details className="preview-provenance"><summary>Preview provenance · {result.identity.caseId} · Build {result.identity.buildId.slice(0,8)}</summary><dl>{Object.entries(result.identity).map(([k,v])=><div key={k}><dt>{k}</dt><dd>{v}</dd></div>)}<dt>Generated</dt><dd>{result.generatedAt}</dd><dt>Sampling</dt><dd>Uniform bin-center · init-sample · not simulation cells</dd><dt>Metadata</dt><dd>{result.core.parameterMetadata?'Core metadata · partial coverage':'Parameter metadata unavailable'}</dd></dl></details>}
  {((currentStatus?.requestId===ownedRequestId&&currentStatus?.diagnostics?.length)||result?.core.diagnostics.length)?<details><summary>Core diagnostics</summary><ul>{((currentStatus?.requestId===ownedRequestId?currentStatus?.diagnostics:undefined)??result?.core.diagnostics??[]).map((d,i)=><li key={i}>{d.severity} · {d.code}: {d.message}</li>)}</ul></details>:null}
 </section><div hidden={!active} className="real-init-inspector">{parameter?<ParameterInspector parameter={parameter} metadata={previewMetadataMatches(result,coreParameters.buildScope,coreParameters.model)&&saved?.text===copy?.text?result?.core.parameterMetadata?.parameters.find(m=>m.key===parameter.key):undefined} revision={previewMetadataMatches(result,coreParameters.buildScope,coreParameters.model)&&saved?.text===copy?.text?result?.identity.configRevision:undefined} candidate={coreParameters.candidate}/>:<aside className="inspector panel" aria-label="Real IC Inspector"><div className="panel-heading"><h2>{uniformState?'Initial state Inspector':'Init sample Inspector'}</h2></div><div className="inspector-content"><p>Source: ARCH initialization</p>{sample!==null&&result?.core.data?<>{uniformState?<p>Uniform initial state · no spatial sample coordinate</p>:<>{rawCoordinates.map(c=><p key={c.axis}>{c.name}: {c.value.toPrecision(8)} · {c.unit??'Unit not provided'} · index {c.index}</p>)}{data?.sampling.fixedCoordinates?.map(c=><p key={c.name}>{c.name}: {c.value} · {c.unit??'Unit not provided'} (inactive coordinate)</p>)}<p>Raw index {sample} · x1-fastest{data?.dimension===3?' · (k*Ny+j)*Nx+i':''}</p></>}<dl>{fields.map(f=><div key={f.key}><dt>{f.displayName||f.key}</dt><dd>{f.values[sample].toPrecision(8)} · {f.unit??'Unit not provided'}</dd></div>)}</dl><p>{result.identity.caseId} · {uniformState?'uniform initial state':'init-sample '+sample}</p><p>Build {result.identity.buildId.slice(0,8)}</p><p>Config {result.identity.configRevision.slice(0,12)}</p><p>Binary {result.identity.binarySha256.slice(0,12)}</p></>:<p>{uniformState?'Inspect the uniform initial state.':'Click a real sample or select its index.'}</p>}</div></aside>}</div></>;
}

function previewStateSummary(state:Record<string,unknown>|null|undefined){
 const grid=state?.grid as {axes?:{name:string;min:number;max:number}[]}|undefined;
 const eos=state?.eos as {status?:string;resolved?:string}|undefined;
 const species=state?.species as {name:string}[]|undefined;
 const amr=state?.amr as {minLevel?:number;maxLevel?:number}|undefined;
 return `Region: ${grid?.axes?.map(a=>a.name+' ['+a.min+', '+a.max+']').join('; ')??'Unavailable'} · EOS: ${eos?.resolved??'Unknown'} / ${eos?.status??'Unknown'} · Species: ${species?.map(s=>s.name).join(', ')??'Unavailable'} · AMR config: ${amr?.minLevel??'?'}–${amr?.maxLevel??'?'}; hierarchy not constructed`;
}
