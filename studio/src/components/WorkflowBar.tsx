import {RunControls} from './RunControls';
import type {WorkingCopy} from '../data/RealInitPreviewProvider';
import {useConfigure} from '../host/useConfigure';
import {useState} from 'react';
import {useHost} from '../host/hostContext';
import {useBuild,BuildOutput} from '../host/BuildProvider';
import {useWorkflow} from '../state/workflowContext';
export function WorkflowBar({copy=null}:{copy?:WorkingCopy|null}){
 const {connected}=useHost();const {status,error,pending,start}=useBuild();const {preview}=useWorkflow();
 const configure=useConfigure();
 const [open,setOpen]=useState(false);
 const building=pending||!!status?.activeBuildId;
 const canBuild=connected&&!!status?.configured&&!building&&!configure.status?.active&&!configure.pending;
 const canConfigure=connected&&!!configure.status?.profileId&&!building&&!configure.pending&&!configure.status.active;
 const configureReason=configure.status?.profileId?'Configure the selected Host-owned build profile. Configuration does not build or update the executable.':'Configure unavailable: select a Host-owned Configure profile.';
 return <footer className="real-workflow" aria-label="Real Config workflow">
  <div className="workflow-actions">
   <button disabled={!canConfigure} title={configureReason} onClick={()=>{setOpen(true);void configure.start();}}>{configure.status?.active?'Configuring…':'Configure'}</button>{configure.status?.active&&<button disabled={configure.pending} onClick={()=>void configure.cancel()}>Cancel Configure</button>}
   <button disabled={!canBuild} title={canBuild?'Build the managed source using the fixed Host profile.':building?'Build is active.':status?.reason??'Connect a configured Build Host.'} onClick={()=>{setOpen(true);void start();}}>{building?'Building…':'Build'}</button>
   <button disabled={!preview.enabled} title={preview.reason} onClick={preview.generate}>Update Preview</button>
   <span role="status">Status · Configure: {configure.status?.active?'running':configure.status?.latest?.state??'not run'} · Build: {status?.state??'unavailable'} · Preview: {preview.state}</span>
   <button aria-expanded={open} aria-controls="workflow-terminal" onClick={()=>setOpen(v=>!v)}>{open?'Hide terminal':'Show terminal'}</button>
  </div>
  <RunControls copy={copy} busy={building||!!configure.status?.active||configure.pending||preview.state==='running'}/>
  <details className="workflow-availability"><summary>Action availability</summary><p>{configureReason}</p><p>Preview: {preview.reason}</p><p>Run / Restart: inspect the saved configuration, confirm the compiled binary and output destination, then start an independent Linux terminal.</p></details>
  {error&&<p role="alert">{error}</p>}{configure.error&&<p role="alert">{configure.error}</p>}{configure.status?.latest?.error&&<p role="alert">{configure.status.latest.error}</p>}
  <section id="workflow-terminal" className="workflow-terminal" hidden={!open} aria-label="Configure / Build terminal">
   <p>Configure / Build output · closing this drawer does not cancel tasks.</p><p>{configureReason}</p><BuildOutput/>{configure.log?.truncated&&<p>Earlier Configure output truncated.</p>}<pre className="build-log" tabIndex={0} aria-label="Configure output">{configure.log?.events.map(e=>'['+e.kind+'] '+(e.text??e.state??'')).join('\n')??'No Configure output.'}</pre>
  </section>
 </footer>;
}
