import {desktop} from '../host/desktop';
import {validateDiscovery} from '../host/workflowClient';
import {InspectionRequests} from '../data/inspectionRequests';
import {useEffect,useRef} from 'react';
import {useCoreParameters} from '../state/coreParameters';
import {useHost} from '../host/hostContext';
import type {WorkingCopy} from '../data/RealInitPreviewProvider';
import {configRevision,previewRequest} from '../data/RealInitPreviewProvider';
import {validateSchemaResponse} from '../host/configurationValidation';
import {sameBuildScope} from '../host/configurationContracts';
import {pairingSuspicion} from '../data/configurationIdentity';
export function ConfigurationBridge({copy}:{copy:WorkingCopy|null}){
 const {connected,snapshot}=useHost();
 const {model,buildScope,configurationScope,setConfigurationScope,setSchema,setInspection,setInspectionMessage,setDiscovery,setModel}=useCoreParameters();
 const requests=useRef(new InspectionRequests());
 const projectId=connected?snapshot?.session.projectId:undefined;
 const binarySha=snapshot?.session.executable?.sha256;
 useEffect(()=>{setConfigurationScope(projectId&&binarySha?{projectId,buildId:'selected-binary:'+binarySha,binarySha256:binarySha}:null);},[projectId,binarySha,setConfigurationScope]);
 const project=configurationScope?.projectId===projectId?configurationScope:null;
 useEffect(()=>{
  if(!buildScope||buildScope.projectId!==projectId)return;let disposed=false;
  void previewRequest('/api/cases').then(v=>{if(!disposed){const registered=validateDiscovery(v,buildScope);setDiscovery(registered);
   if(desktop?.selectedSource&&snapshot?.session.caseSource?.sha256){
    const absolute=snapshot.session.projectRoot+'/'+desktop.selectedSource;
    const matches=registered.cases.filter(c=>(c.inspection.sourceFile===absolute||c.inspection.sourceFile===desktop?.selectedSource)
     &&c.inspection.compiledSourceSha256===snapshot.session.caseSource?.sha256);
    if(matches.length===1)setModel(matches[0].caseId);
   }
  }}).catch(()=>{if(!disposed)setDiscovery(null);});
  return()=>{disposed=true;};
 },[buildScope,projectId,setDiscovery,setModel,snapshot?.session.projectRoot,snapshot?.session.caseSource?.sha256]);
 const text=copy?.text;
 useEffect(()=>{
  if(!project)return;let disposed=false;
  void previewRequest('/api/configuration/schema').then(v=>{const s=validateSchemaResponse(v,project);if(!disposed)setSchema(s);}).catch(e=>{if(!disposed)setInspectionMessage(e instanceof Error?e.message:'Schema unavailable');});
  return()=>{disposed=true;};
 },[project,setSchema,setInspectionMessage,setDiscovery]);
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
 const {model,discovery,buildScope}=useCoreParameters();
 const registered=sameBuildScope(discovery,buildScope)?discovery?.cases.find(c=>c.caseId===model):undefined;
 const warning=pairingSuspicion(model,copy?.filename??'');
 return <section className="configuration-identity" aria-label="Current configuration identity">{warning&&<p className="pairing-warning">{warning}</p>}<details><summary>Source &amp; parameter association</summary><p>Source: {registered?.inspection.sourceFile??'Unavailable until binary discovery'} · compiled source association; build freshness remains separately reported</p><p>Parameter path: {copy?.hostPath??'Browser import / no trusted Host path'}</p><p className="section-note">Model / parameter association unconfirmed; filenames do not verify compatibility.</p></details></section>;
}
// Shared by editors and the preview action; stale inspection cannot validate a new Working Copy.
// eslint-disable-next-line react-refresh/only-export-components
export function currentInspection(core:ReturnType<typeof useCoreParameters>,text:string){
 const inspection=core.inspection;
 return inspection&&inspection.text===text&&inspection.response.identity.caseId===core.model&&sameBuildScope(inspection.response.identity,core.configurationScope)?inspection.response.core:undefined;
}
