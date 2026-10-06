import {RunHistory} from './RunHistory';
import {useEffect,useRef,useState} from 'react';
import {useHost} from '../host/hostContext';
import {useCoreParameters} from '../state/coreParameters';
import {buildRequest} from '../host/BuildAdapter';
import {validateRunPreparation,validateRunAcceptance,validateRunStatus} from '../host/RunAdapter';
import type {RunPreparation,RunAcceptance} from '../host/runContracts';
import type {WorkingCopy} from '../data/RealInitPreviewProvider';
import {configRevision} from '../data/RealInitPreviewProvider';
export function RunControls({copy,busy}:{copy:WorkingCopy|null;busy:boolean}){
 const {snapshot,connected}=useHost(),{model}=useCoreParameters();
 const project=snapshot?.session,projectId=connected?project?.projectId:undefined;
 const configPath=project?.parameterFile?.relativePath,binaryPath=project?.executable?.relativePath;
 const reason=!projectId?'Connect a Local Host.':!copy?'Open the project configuration.':
  !configPath||copy.hostPath!==project?.projectRoot+'/'+configPath?'Save or open the associated project configuration first.':
  copy.dirty?'Save the exact Working Copy before Run / Restart.':!binaryPath?'Select the ARCH executable.':
  busy?'Wait for the active Configure / Build / Preview operation.':'Inspect the saved configuration with the selected compiled binary.';
 const eligible=!!(projectId&&copy&&!copy.dirty&&configPath&&copy.hostPath===project?.projectRoot+'/'+configPath&&binaryPath&&!busy);
 const scope=JSON.stringify([projectId,model,copy?.text,copy?.dirty,copy?.hostPath,configPath,binaryPath,project?.executable?.sha256]);
 const epoch=useRef({value:0});
 const [candidate,setCandidate]=useState<{scope:string;plan:RunPreparation}>();
 const [pending,setPending]=useState<{scope:string;kind:string}>();
 const [message,setMessage]=useState<{projectId:string;text:string}>();
 const [accepted,setAccepted]=useState<Record<string,RunAcceptance&{caseId:string;configPath:string;configSha:string}>>({});
 const [confirmed,setConfirmed]=useState(false);
 const dialog=useRef<HTMLElement>(null);
 const [seenScope,setSeenScope]=useState(scope);
 // Discard the confirmation when its editing context changes, even if later Undo restores identical bytes.
 if(seenScope!==scope){setSeenScope(scope);setCandidate(undefined);setConfirmed(false);setPending(undefined);}

 const plan=candidate?.scope===scope?candidate.plan:undefined;
 const active=projectId?accepted[projectId]:undefined;
 const waiting=pending?.scope===scope;
 useEffect(()=>{const tracker=epoch.current;++tracker.value;return()=>{++tracker.value;};},[scope]);
 useEffect(()=>{if(plan)dialog.current?.focus();},[plan]);
 const activeRunId=active?.runId,runFinished=active?.state.finishedAt;
 useEffect(()=>{
  if(!projectId||!activeRunId||runFinished)return;
  let gone=false,timer:ReturnType<typeof setTimeout>;
  const runId=activeRunId;
  async function poll(){
   try{
    const state=validateRunStatus(await buildRequest('/api/run/'+runId),projectId!,runId);
    if(!gone)setAccepted(previous=>previous[projectId!]?.runId===runId?{...previous,[projectId!]:{...previous[projectId!],state}}:previous);
   }catch(e){if(!gone)setMessage({projectId:projectId!,text:e instanceof Error?e.message:'Run status unavailable; the independent terminal may still be active.'});}
   finally{if(!gone)timer=setTimeout(()=>void poll(),1000);}
  }
  void poll();return()=>{gone=true;clearTimeout(timer);};
 },[projectId,activeRunId,runFinished]); // Only the owned run is polled, never Preview.
 async function prepare(mode:'run'|'restart'){
  if(!eligible||!projectId||!copy||!configPath||!binaryPath||waiting)return;
  const ticket=epoch.current.value;setPending({scope,kind:'preparing'});setCandidate(undefined);setConfirmed(false);setMessage(undefined);
  try{
   const revision=await configRevision(copy.text);
   if(ticket!==epoch.current.value)return;
   const request={projectId,caseId:model,configRevision:revision,mode};
   const result=validateRunPreparation(await buildRequest('/api/run/prepare',request),{...request,configPath,binaryPath});
   if(ticket===epoch.current.value)setCandidate({scope,plan:result});
  }catch(e){if(ticket===epoch.current.value)setMessage({projectId,text:e instanceof Error?e.message:'Run preparation failed.'});}
  finally{if(ticket===epoch.current.value)setPending(undefined);}
 }
 async function start(){
  if(!plan||!plan.canConfirm||!confirmed||!projectId||waiting||!eligible)return;
  const ticket=epoch.current.value,selected=plan;
  setPending({scope,kind:'starting'});setMessage(undefined);
  try{
   const result=validateRunAcceptance(await buildRequest('/api/run',{projectId,planId:selected.planId,confirmation:'run-saved-input-with-compiled-binary'}),projectId);
   // A delivered run is an independent computation; later config edits must not cancel or relabel it.
   setAccepted(previous=>({...previous,[projectId]:{...result,caseId:selected.caseId,configPath:selected.config.relativePath,configSha:selected.config.fingerprint.sha256}}));
   if(ticket===epoch.current.value){setCandidate(undefined);setConfirmed(false);}
  }catch(e){
   if(ticket===epoch.current.value){setCandidate(undefined);setConfirmed(false);}
   setMessage({projectId,text:'Terminal handoff not confirmed. Check its terminal/local run record before preparing again. '+(e instanceof Error?e.message:'Request failed.')});
  }
  finally{if(ticket===epoch.current.value)setPending(undefined);}
 }
 async function stop(){
  if(!active||!projectId||waiting||active.state.finishedAt)return;
  const ticket=epoch.current.value;setPending({scope,kind:'stopping'});
  try{
   await buildRequest('/api/run/'+active.runId+'/stop',undefined,'POST');
   setMessage({projectId,text:'Stop requested for '+active.runId+'. Waiting for its recorded exit.'});
  }catch(e){setMessage({projectId,text:e instanceof Error?e.message:'Stop request failed.'});}
  finally{if(ticket===epoch.current.value)setPending(undefined);}
 }
 return <section className="run-controls" aria-label="Local Run and Restart">
  <div className="workflow-actions">
   <button disabled={!eligible||waiting} title={reason} onClick={()=>void prepare('run')}>Run</button>
   <button disabled={!eligible||waiting} title={reason+' Restart uses restart_file in the saved configuration.'} onClick={()=>void prepare('restart')}>Restart from checkpoint</button>
   <span role="status">{waiting?pending.kind+'…':active?'Latest run · '+active.state.state:reason}</span>
   {active&&!active.state.finishedAt&&<button disabled={waiting} onClick={()=>void stop()}>Stop run</button>}
  </div>
  {message&&message.projectId===projectId&&<p role="status">{message?.text}</p>}
  {active&&<details><summary>Latest independent run · {active.runId}</summary><p>Model: {active.caseId} · Saved config: {active.configPath} · SHA-256: <code>{active.configSha}</code></p><p>Terminal PID: {active.terminalPid} · Core PID: {active.state.processId??'not started'} · Exit: {active.state.exitCode??active.state.signal??'pending'}</p>{active.state.error&&<p role="alert">{active.state.error}</p>}<p>Closing Studio or changing configuration does not stop this run. Full output remains in its terminal and local run log.</p></details>}
  <RunHistory projectId={projectId} refreshKey={activeRunId}/>
  {plan&&<section ref={dialog} tabIndex={-1} role="dialog" aria-label="Confirm local computation" className="run-confirmation" onKeyDown={e=>{if(e.key==='Escape'&&!waiting){setCandidate(undefined);setConfirmed(false);}}}>
   <h3>{plan.mode==='run'?'Run saved configuration':'Restart from saved checkpoint selection'}</h3>
   <p>Model: <strong>{plan.caseId}</strong> · Config: <code>{plan.config.relativePath}</code></p>
   <p>Executable: <code>{plan.binary.relativePath}</code>. This runs the selected compiled version; current-source freshness is not claimed.</p>
   {plan.checkpointPath&&<p>Checkpoint: <code>{plan.checkpointPath}</code></p>}
   {plan.pathChecks.filter(p=>p.role==='output-directory').map(p=><p key={p.key}>{p.key}: <code>{p.resolvedPath??'not resolved'}</code>{p.exists?' · existing directory; Core may write files here':''}</p>)}
   <p>Core startup still checks: {plan.pendingChecks.join('; ')}. No simulation has started.</p>
   {!!plan.issues.length&&<ul role="alert">{plan.issues.map((issue,i)=><li key={i}>{issue}</li>)}</ul>}
   {plan.inspection.diagnostics.filter(d=>d.severity==='error').map((d,i)=><p role="alert" key={i}>{d.parameterKey??'Configuration'}: {d.message}</p>)}
   <details><summary>Confirmed input and executable identity</summary><p>Config SHA-256: <code>{plan.config.fingerprint.sha256}</code></p><p>Binary SHA-256: <code>{plan.binary.fingerprint.sha256}</code></p></details>
   {plan.canConfirm&&<label><input type="checkbox" checked={confirmed} disabled={waiting} onChange={e=>setConfirmed(e.target.checked)}/> I confirm this saved input, compiled binary and output destination.</label>}
   <div className="workflow-actions"><button disabled={!confirmed||!plan.canConfirm||waiting||!eligible} onClick={()=>void start()}>Start in independent terminal</button><button disabled={waiting} onClick={()=>{setCandidate(undefined);setConfirmed(false);}}>Cancel</button></div>
  </section>}
 </section>;
}
