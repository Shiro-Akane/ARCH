import type { WorkingCopy } from '../data/RealInitPreviewProvider';
import { useCoreParameters } from '../state/coreParameters';
import { workspaceOptions } from '../data/workspacePresentation';
import type { Workspace } from '../data/workspacePresentation';

export function WorkspaceHeader({source,onSource,copy,debugSamples}:{source:Workspace;onSource:(source:Workspace)=>void;copy:WorkingCopy|null;debugSamples:boolean}) {
 const {model}=useCoreParameters();
 const config=source==='config',plotfile=source==='plotfile';
 const modelLabel=config?model:plotfile?'Plotfile':source==='cellular'?'CellularDet snapshot':'Hotspot demo';
 const fileLabel=config?copy?.filename??'No parameter file':plotfile?'Read-only file':'Archived sample';
 return <header className="topbar">
  <div className="brand" aria-label="ARCH Studio"><svg width="27" height="28" viewBox="0 0 27 28" aria-hidden="true"><path d="M3 23 13.5 4 24 23h-7l-3.5-7-3.5 7Z" fill="currentColor" /></svg><span>ARCH<span className="brand-light">STUDIO</span></span></div>
  <span className="header-divider" />
  <div className="document-meta" aria-label="Current document">
   <span className="meta-label">{config?'MODEL':'DATA'}</span><strong className="document-model" title={config?'Selected registered model; parameter association is checked separately.':modelLabel}>{modelLabel}</strong>
   <span className="meta-slash">/</span><span className="meta-label">{config?'PARAMETERS':'SOURCE'}</span><span className="document-file" title={fileLabel}>{fileLabel}</span>
   {config&&copy?.dirty&&<span className="document-dirty" role="status">Unsaved</span>}
  </div>
  <div className="topbar-right">
   <label className="workspace-choice"><span>Workspace</span><select aria-label="Workspace" value={source} onChange={e=>onSource(e.target.value as Workspace)}>
    {workspaceOptions.map(option=><option key={option.value} value={option.value}>{option.label}</option>)}
    {debugSamples&&<><option value="cellular">Debug · archived Cellular result</option><option value="mock">Debug · Hotspot illustration</option></>}
   </select></label>
   {!config&&<span className="shell-badge">{plotfile?'Read only':'Debug sample'}</span>}
  </div>
 </header>;
}
