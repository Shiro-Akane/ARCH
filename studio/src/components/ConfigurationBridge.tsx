import {InspectionRequests} from '../data/inspectionRequests';
import {useEffect,useRef} from 'react';
import {useCoreParameters} from '../state/coreParameters';
import {useHost} from '../host/hostContext';
import type {WorkingCopy} from '../data/RealInitPreviewProvider';
import {configRevision,previewRequest} from '../data/RealInitPreviewProvider';
import {validateSchemaResponse} from '../host/configurationValidation';
import {sameBuildScope} from '../host/configurationContracts';
import {modelSource,pairingSuspicion} from '../data/configurationIdentity';
export function ConfigurationBridge({copy}:{copy:WorkingCopy|null}){
 const {connected,snapshot}=useHost();
 const {model,buildScope,setSchema,setInspection,setInspectionMessage}=useCoreParameters();
 const requests=useRef(new InspectionRequests());
 const projectId=connected?snapshot?.session.projectId:undefined;
 const project=buildScope?.projectId===projectId?buildScope:null;
 const text=copy?.text;
 useEffect(()=>{
  if(!project)return;let disposed=false;
  void previewRequest('/api/configuration/schema').then(v=>{const s=validateSchemaResponse(v,project);if(!disposed)setSchema(s);}).catch(e=>{if(!disposed)setInspectionMessage(e instanceof Error?e.message:'Schema unavailable');});
  return()=>{disposed=true;};
 },[project,setSchema,setInspectionMessage]);
 useEffect(()=>{
  const gate=requests.current;const ticket=gate.begin();
  if(!project||text===undefined)return;
  let disposed=false;
  const timer=setTimeout(()=>{
   setInspectionMessage('Inspecting current Working Copy…');
   void (async()=>{
    const configRequest={projectId:project.projectId,caseId:model,configText:text,configRevision:await configRevision(text)};
    if(disposed)return;
    const response=gate.accept(ticket,await previewRequest('/api/configuration/inspect',configRequest),configRequest,project);
    if(disposed||!response)return;
    setInspection({text,response});setInspectionMessage(response.core.status==='ok'?'Input inspection complete · before Setup, not simulation validation':'Core reports configuration errors');
   })().catch(e=>{if(!disposed)setInspectionMessage(e instanceof Error?e.message:'Inspection unavailable');});
  },350);
  return()=>{disposed=true;gate.invalidate();clearTimeout(timer);};
 },[project,model,text,setInspection,setInspectionMessage]);
 return null;
}
export function ConfigurationIdentity({copy}:{copy:WorkingCopy|null}){
 const {model}=useCoreParameters();const {snapshot}=useHost();
 const warning=pairingSuspicion(model,copy?.filename??'');
 return <section className="configuration-identity" aria-label="Current configuration identity"><div><strong>Current Model: {model}</strong><strong>Parameter File: {copy?.filename??'Not loaded'}</strong></div><p>Source: {snapshot?.session.projectRoot?`${snapshot.session.projectRoot}/`:''}{modelSource[model]} · configured association</p><p>Parameter path: {copy?.hostPath??'Browser import / no trusted Host path'}</p><p className={warning?'pairing-warning':'section-note'}>{warning??'Model / parameter association unconfirmed; filenames do not verify compatibility.'}</p></section>;
}
// Shared by editors and the preview action; stale inspection cannot validate a new Working Copy.
// eslint-disable-next-line react-refresh/only-export-components
export function currentInspection(core:ReturnType<typeof useCoreParameters>,text:string){
 const inspection=core.inspection;
 return inspection&&inspection.text===text&&inspection.response.identity.caseId===core.model&&sameBuildScope(inspection.response.identity,core.buildScope)?inspection.response.core:undefined;
}
