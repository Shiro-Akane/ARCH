import {useEffect,useLayoutEffect,useRef,useState} from 'react';
import {useCoreParameters} from '../../state/coreParameters';
import {sameBuildScope} from '../../host/configurationContracts';
import {acceptWorkflowResult,validateWorkflowAck,validateWorkflowStatus} from '../../host/workflowClient';
import {previewRequest,configRevision} from '../../data/RealInitPreviewProvider';
import type {WorkingCopy} from '../../data/RealInitPreviewProvider';
import type {WorkflowOperation,WorkflowResult,AmrMesh,CaseProbe,ResourceEstimate} from '../../host/workflowContracts';
import {ResourceTable} from './ResourceTable';
import {effectiveEntries,parsePar} from '../../data/ParDocument';
export function InitializationWorkflow({copy,fieldBusy,buildReady,onBusy,onMesh}:{buildReady:boolean;copy:WorkingCopy|null;fieldBusy:boolean;onBusy:(busy:boolean)=>void;onMesh:(result:WorkflowResult)=>void}){
 const core=useCoreParameters(),scope=core.buildScope;
 const currentBuild=buildReady&&!!scope&&!scope.buildId.startsWith('selected-binary:');
 const discovery=sameBuildScope(core.discovery,scope)?core.discovery:null;
 const model=discovery?.cases.find(c=>c.caseId===core.model);
 const [busy,setBusy]=useState(false),[message,setMessage]=useState(''),[error,setError]=useState('');
 const [results,setResults]=useState<Partial<Record<WorkflowOperation,{result:WorkflowResult;text:string}>>>({});
 const [maxBlocks,setMaxBlocks]=useState(''),[memory,setMemory]=useState('');
 const generation=useRef(0),owned=useRef<string|null>(null),alive=useRef(true);
 const current=useRef({text:copy?.text,caseId:core.model,scope});
 useLayoutEffect(()=>{current.current={text:copy?.text,caseId:core.model,scope};},[copy?.text,core.model,scope]);
 useEffect(()=>{alive.current=true;const gate=generation;return()=>{alive.current=false;gate.current++;if(owned.current)void previewRequest('/api/workflow/'+owned.current+'/cancel',undefined,true).catch(()=>{});};},[]);
 const isCurrent=(r:{result:WorkflowResult;text:string})=>r.text===copy?.text&&r.result.identity.caseId===core.model&&sameBuildScope(r.result.identity,scope);
 async function run(operation:WorkflowOperation){
  if(!currentBuild||!scope||!copy?.valid||!discovery||busy||fieldBusy)return;
  const ticket=++generation.current,start={text:copy.text,caseId:core.model,scope};
  setBusy(true);onBusy(true);setError('');setMessage(operation==='inspect-case'?'Inspecting initialization…':operation==='preview-amr'?'Constructing initial AMR…':'Estimating full-domain resources…');
  try{
   const revision=await configRevision(start.text);
   const response=await previewRequest('/api/workflow',{projectId:scope.projectId,caseId:start.caseId,configText:start.text,configRevision:revision,operation,...(operation==='preview-amr'?{meshMaxBlocks:Number(maxBlocks||discovery.amr?.defaultMaxBlocks),meshMemoryMiB:Number(memory||discovery.amr?.defaultMemoryMiB)}:{})});
   const identity=validateWorkflowAck(response,scope,start.caseId,revision);
   if(ticket!==generation.current||!alive.current){await previewRequest('/api/workflow/'+identity.requestId+'/cancel',undefined,true).catch(()=>{});return;}
   owned.current=identity.requestId;
   const until=Date.now()+(operation==='inspect-case'?(model?.inspection.hostWallTimeoutSeconds??360)+15:60)*1000;
   while(Date.now()<until){
    const s=validateWorkflowStatus(await previewRequest('/api/workflow/status'),scope.projectId);
    if(ticket!==generation.current||!alive.current)return;
    if(s.requestId!==identity.requestId)throw new Error('Workflow superseded; late result discarded.');
    if(s.state==='running'){setMessage('Working · '+(s.stage??'preparing')+'…');await new Promise(r=>setTimeout(r,200));continue;}
    if(s.state!=='succeeded')throw new Error(s.error??'Workflow failed; previous result retained.');
    const accepted=acceptWorkflowResult(s.result,identity,operation);
    if(start.text!==current.current.text||start.caseId!==current.current.caseId||!sameBuildScope(start.scope,current.current.scope))throw new Error('Working Copy, model or build changed; late result discarded.');
    setResults(old=>({...old,[operation]:{result:accepted,text:start.text}}));if(operation==='preview-amr')onMesh(accepted);
    setMessage(accepted.core.status==='limited'?'Limited / Incomplete · preview budget reached':'Current · '+(operation==='inspect-case'?'initialization inspection':operation==='preview-amr'?'initial AMR hierarchy':'resource estimate'));return;
   }
   throw new Error('Workflow wall timeout; previous result retained.');
  }catch(e){if(ticket===generation.current&&alive.current){if(owned.current)await previewRequest('/api/workflow/'+owned.current+'/cancel',undefined,true).catch(()=>{});setError(e instanceof Error?e.message:'Workflow failed.');setMessage('Previous result retained.');}}
  finally{if(ticket===generation.current){owned.current=null;if(alive.current){setBusy(false);onBusy(false);}}}
 }
 async function cancel(){generation.current++;setMessage('Cancelling initialization workflow…');try{if(owned.current)await previewRequest('/api/workflow/'+owned.current+'/cancel',undefined,true);setMessage('Cancelled; previous result retained.');}catch(e){setError(e instanceof Error?e.message:'Cancellation failed.');}finally{owned.current=null;setBusy(false);onBusy(false);}}
 const disabled=!currentBuild||!scope||!copy?.valid||!model||busy||fieldBusy;
 const inspection=results['inspect-case'],resources=results['amr-resources'],mesh=results['preview-amr'];
 const values=new Map(effectiveEntries(parsePar(copy?.text??'')).map(e=>[e.key,e.value]));
 const budgetValid=Number.isInteger(Number(maxBlocks||discovery?.amr?.defaultMaxBlocks))&&Number(maxBlocks||discovery?.amr?.defaultMaxBlocks)>=1&&Number(maxBlocks||discovery?.amr?.defaultMaxBlocks)<=1024&&Number.isInteger(Number(memory||discovery?.amr?.defaultMemoryMiB))&&Number(memory||discovery?.amr?.defaultMemoryMiB)>=16&&Number(memory||discovery?.amr?.defaultMemoryMiB)<=256;
 return <section className="initialization-workflow" aria-label="Case inspection and initial AMR"><h3>Initialization &amp; initial AMR</h3>
 {model?<p>Registered: {model.caseId} · Field Preview {model.initialFieldPreview?'supported':'unavailable'} · Initial AMR {model.initialAmrPreview?'supported':'unavailable'} · Dimensions {model.previewDimensions.join(', ')||'inspection only'} · Geometry {discovery?.fieldModels.find(m=>m.caseId===model.caseId)?.geometries.join(', ')||'reported by inspection'}</p>:<p>Selected binary registry unavailable. Connect a current Build.</p>}
 <p>Build {scope?.buildId.slice(0,8)??'unavailable'} · binary {scope?.binarySha256.slice(0,12)??'unavailable'} · {currentBuild?'tracked inputs match successful Build; complete dependency freshness unknown.':'selected binary only; current tracked-input Build validation unavailable. Initialization requires a current successful Build.'}</p>
 <div className="real-preview-actions"><button disabled={disabled||!model?.inspection.setupReads} onClick={()=>void run('inspect-case')}>Inspect initialization</button><button disabled={disabled||!discovery?.amr} onClick={()=>void run('amr-resources')}>Estimate AMR resources</button><button disabled={!busy} onClick={()=>void cancel()}>Cancel initialization</button></div>
 {model?.initialAmrPreview&&<><p>AMR preview budget · separate from .par max_blocks. Core v1: 1–1024 blocks, 16–256 MiB.</p><div className="real-preview-actions"><label>Preview max blocks <input aria-label="AMR preview max blocks" type="number" min={1} max={1024} value={maxBlocks||discovery?.amr?.defaultMaxBlocks||''} onChange={e=>setMaxBlocks(e.target.value)}/></label><label>Preview memory MiB <input aria-label="AMR preview memory MiB" type="number" min={16} max={256} value={memory||discovery?.amr?.defaultMemoryMiB||''} onChange={e=>setMemory(e.target.value)}/></label><button disabled={disabled||!budgetValid} onClick={()=>void run('preview-amr')}>Generate initial AMR</button></div></>}
 <p role="status">{message.startsWith('Current ·')&&!Object.values(results).some(isCurrent)?'Previous results / stale · Working Copy, model or build changed.':message}</p>{error&&<p role="alert">{error}</p>}
 {inspection&&<details open><summary>Initialization inspection · {isCurrent(inspection)?'Current':'Previous result / stale'}</summary><p>Setup/read coverage only; small raw Init probes, not full field or AMR cell values. Units are Core-reviewed evidence, not automatic C++ inference.</p><p>{inspection.result.core.parameterMetadata?.coverage} · {inspection.result.core.parameterMetadata?.unobservedMeaning}</p>
 <p>Unobserved keys: {inspection.result.core.parameterMetadata?.unobservedInputKeys.join(', ')||'None reported'}</p>
 <div style={{overflowX:'auto'}}><table><thead><tr>{['Parameter / edit Working Copy','Explicit','Default','Effective','Source','Unit evidence'].map(h=><th key={h}>{h}</th>)}</tr></thead><tbody>{inspection.result.core.parameterMetadata?.parameters.map(p=><tr key={p.key}><td><label>{p.key}<input aria-label={'Observed '+p.key} disabled={inspection.result.identity.caseId!==core.model||!sameBuildScope(inspection.result.identity,scope)} value={values.get(p.key)??''} placeholder="Absent from Working Copy" onChange={e=>core.edit(p.key,e.target.value)}/></label></td><td>{show(p.explicitValue)}</td><td>{show(p.defaultValue)}</td><td>{show(p.effectiveValue)}</td><td>{p.valueSource}{p.sourceReason?' / '+p.sourceReason:''}</td><td>{p.unit??'Unknown'} · {show(p.unitEvidence.status)}<details><summary>Evidence</summary><pre>{JSON.stringify(p.unitEvidence,null,2)}</pre></details></td></tr>)}</tbody></table></div>
 {inspection.result.core.data&&'samples' in inspection.result.core.data&&<Probe data={inspection.result.core.data as CaseProbe}/>}
 <details><summary>Compiled source / state / diagnostics</summary><pre>{JSON.stringify({identity:inspection.result.identity,capability:inspection.result.core.capability,state:inspection.result.core.state,diagnostics:inspection.result.core.diagnostics},null,2)}</pre></details></details>}
 {resources&&<details open><summary>Resource estimate · {isCurrent(resources)?'Current':'Previous result / stale'}</summary><ResourceTable data={resources.result.core.data as ResourceEstimate}/></details>}
 {mesh&&<details open><summary>Initial AMR · {isCurrent(mesh)?(mesh.result.core.status==='limited'?'Limited / Incomplete':'Complete'):'Previous result / stale'}</summary><MeshSummary mesh={mesh.result.core.data as AmrMesh}/><details><summary>Mesh provenance / state / diagnostics</summary><pre>{JSON.stringify({identity:mesh.result.identity,state:mesh.result.core.state,diagnostics:mesh.result.core.diagnostics},null,2)}</pre></details></details>}
 </section>;
}
const show=(value:unknown)=>value===null||value===undefined?'Unavailable':typeof value==='object'?JSON.stringify(value):String(value);
function Probe({data}:{data:CaseProbe}){return <details><summary>Raw Init probes · {data.sampleCount} samples</summary><p>{data.valueLocation}. {data.sampling}. Velocity: {data.velocityBasis}.</p>{data.samples.map((s,i)=><div key={i}><h5>Probe {i} · ({s.cartesianPosition.join(', ')}) {s.positionUnit}</h5><p>Thermodynamic input: {s.thermodynamicInput}</p><dl>{s.fields.map(f=><div key={f.key}><dt>{f.key} · {f.consumedByConversion?'consumed input':'unused by conversion'}</dt><dd>{f.value} {f.unit??'Unit unknown'}</dd></div>)}</dl><p>Mass fractions: {s.massFractions.join(', ')} ({s.massFractionUnit})</p></div>)}</details>;}
function MeshSummary({mesh}:{mesh:AmrMesh}){return <><p>{mesh.snapshot==='none'?'No hierarchy snapshot: root grid exceeds preview budget.':mesh.complete?'Complete initial hierarchy':'Limited / Incomplete: last completed balanced hierarchy'}</p><p>{mesh.limitedReason??'No preview limit reached'} · Completed passes: {mesh.completedPasses} · Configured max_blocks: {mesh.configuredMaxBlocks} · Preview working capacity: {mesh.workingCapacity}</p>{mesh.snapshot!=='none'&&<p>{mesh.leafCount} leaf blocks · {mesh.levelCounts.map(l=>'L'+l.level+': '+l.leafBlocks).join(' · ')}</p>}<ResourceTable data={mesh.resources}/></>;}
