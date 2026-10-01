import {useState} from 'react';
import {useHost} from '../host/hostContext';
import {useBuild,BuildOutput} from '../host/BuildProvider';
import {useWorkflow} from '../state/workflowContext';
export function WorkflowBar(){
 const {snapshot,connected}=useHost();const {status,error,pending,start}=useBuild();const {preview}=useWorkflow();
 const [open,setOpen]=useState(false);
 const building=pending||!!status?.activeBuildId;
 const canBuild=connected&&!!snapshot?.host.capabilities.build&&!!status?.configured&&!building;
 const configureReason=status?.configured?'Existing Host-owned build tree configured. Standalone Configure is not exposed by this Host profile.':'Configure unavailable: a Host-owned build profile and existing build tree are required.';
 return <footer className="real-workflow" aria-label="Real Config workflow">
  <div className="workflow-actions">
   <button disabled title={configureReason}>Configure</button>
   <button disabled={!canBuild} title={canBuild?'Build the managed source using the fixed Host profile.':building?'Build is active.':status?.reason??'Connect a configured Build Host.'} onClick={()=>{setOpen(true);void start();}}>{building?'Building…':'Build'}</button>
   <button disabled={!preview.enabled} title={preview.reason} onClick={preview.generate}>Update Preview</button>
   <button disabled title="Local simulation execution belongs to Phase 3C.">Run</button>
   <button disabled title="Checkpoint restart execution belongs to Phase 3C.">Restart from checkpoint</button>
   <span role="status">Status · Build: {status?.state??'unavailable'} · Preview: {preview.state}</span>
   <button aria-expanded={open} aria-controls="workflow-terminal" onClick={()=>setOpen(v=>!v)}>{open?'Hide terminal':'Show terminal'}</button>
  </div>
  <details className="workflow-availability"><summary>Action availability</summary><p>{configureReason}</p><p>Preview: {preview.reason}</p><p>Run / Restart: unavailable in Phase 3B; no simulation is launched.</p></details>
  {error&&<p role="alert">{error}</p>}
  <section id="workflow-terminal" className="workflow-terminal" hidden={!open} aria-label="Configure / Build terminal">
   <p>Configure / Build output · closing this drawer does not cancel tasks.</p><p>{configureReason}</p><BuildOutput/>
  </section>
 </footer>;
}
