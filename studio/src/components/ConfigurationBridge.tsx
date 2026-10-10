import {desktop} from '../host/desktop';
import {validateDiscovery} from '../host/workflowClient';
import {InspectionRequests} from '../data/inspectionRequests';
import {useEffect,useMemo,useRef,useState} from 'react';
import {useCoreParameters} from '../state/coreParameters';
import {useHost} from '../host/hostContext';
import type {WorkingCopy} from '../data/RealInitPreviewProvider';
import {configRevision,previewRequest} from '../data/RealInitPreviewProvider';
import {validateSchemaResponse} from '../host/configurationValidation';
import {sameBuildScope} from '../host/configurationContracts';
import type {ConfigurationBuildScope} from '../host/configurationContracts';
import {pairingSuspicion,selectedSourceModels} from '../data/configurationIdentity';
export function ConfigurationBridge({copy}:{copy:WorkingCopy|null}){
 const {connected,snapshot}=useHost();
 const {model,discovery,buildScope,configurationScope,setConfigurationScope,setSchema,setInspection,setInspectionMessage,setDiscovery,setModel}=useCoreParameters();
 const requests=useRef(new InspectionRequests());
 const [discoveryAttempt,setDiscoveryAttempt]=useState(0);
 const [discoveryResult,setDiscoveryResult]=useState<{request:{scope:ConfigurationBuildScope;attempt:number};error:string|null}|null>(null);
 const projectId=connected?snapshot?.session.projectId:undefined;
 const binarySha=snapshot?.session.executable?.sha256;
 const discoveryRequest=useMemo(()=>buildScope&&buildScope.projectId===projectId?{scope:buildScope,attempt:discoveryAttempt}:null,[buildScope,projectId,discoveryAttempt]);
 const currentDiscoveryResult=discoveryResult?.request===discoveryRequest?discoveryResult:null;
 const discoveryPending=!!discoveryRequest&&!currentDiscoveryResult;
 const discoveryFailed=!!currentDiscoveryResult&&currentDiscoveryResult.error!==null;
 useEffect(()=>{setConfigurationScope(projectId&&binarySha?{projectId,buildId:'selected-binary:'+binarySha,binarySha256:binarySha}:null);},[projectId,binarySha,setConfigurationScope]);
 const project=configurationScope?.projectId===projectId?configurationScope:null;
 const sourceModels=selectedSourceModels(desktop?.selectedSource,connected?snapshot?.session:null,sameBuildScope(discovery,buildScope)?discovery:null,desktop?.caseId);
 const boundCase=sourceModels.state==='fixed'?sourceModels.caseId:undefined;
 const pendingSource=sourceModels.state==='pending'?sourceModels.message:undefined;
 const modelReady=sourceModels.state==='unbound'||sourceModels.state==='fixed'&&model===boundCase;
 useEffect(()=>{
  if(boundCase&&model!==boundCase)setModel(boundCase);
  if(pendingSource){setInspection(null);setInspectionMessage(pendingSource);}
 },[boundCase,model,pendingSource,setModel,setInspection,setInspectionMessage]);
 useEffect(()=>{
  if(!discoveryRequest)return;let disposed=false;
  // A failed read remains pending until an explicit retry or a genuine scope change.
  void previewRequest('/api/cases').then(v=>{
   if(disposed)return;
   const registry=validateDiscovery(v,discoveryRequest.scope);
   setDiscovery(registry);setDiscoveryResult({request:discoveryRequest,error:null});
  }).catch(e=>{
   if(disposed)return;
   setDiscovery(null);setDiscoveryResult({request:discoveryRequest,error:e instanceof Error?e.message:'Model discovery unavailable.'});
  });
  return()=>{disposed=true;};
 },[discoveryRequest,setDiscovery]);
 const text=copy?.text;
 useEffect(()=>{
  if(!project)return;let disposed=false;
  void previewRequest('/api/configuration/schema').then(v=>{const s=validateSchemaResponse(v,project);if(!disposed)setSchema(s);}).catch(e=>{if(!disposed)setInspectionMessage(e instanceof Error?e.message:'Schema unavailable');});
  return()=>{disposed=true;};
 },[project,setSchema,setInspectionMessage,setDiscovery]);
 useEffect(()=>{
  const gate=requests.current;const ticket=gate.begin();
  if(!project||text===undefined||!modelReady)return;
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
 },[project,model,text,modelReady,setInspection,setInspectionMessage]);
 return discoveryRequest&&(discoveryPending||discoveryFailed)?<section className="configuration-identity" aria-label="Model discovery"><p role="status">{discoveryPending?'Discovering models in the current binary…':`Model discovery unavailable: ${currentDiscoveryResult?.error}`}</p><button type="button" disabled={discoveryPending} onClick={()=>setDiscoveryAttempt(attempt=>attempt+1)}>Retry model discovery</button></section>:null;
}
export function ConfigurationIdentity({copy}:{copy:WorkingCopy|null}){
 const {connected,snapshot}=useHost();
 const {model,discovery,buildScope}=useCoreParameters();
 const sourceModels=selectedSourceModels(desktop?.selectedSource,connected?snapshot?.session:null,sameBuildScope(discovery,buildScope)?discovery:null,desktop?.caseId);
 const displayedModel=sourceModels.state==='pending'?'Pending selected source':sourceModels.state==='fixed'?sourceModels.caseId:model;
 const registered=sourceModels.cases.find(c=>c.caseId===displayedModel);
 const warning=pairingSuspicion(model,copy?.filename??'');
 return <section className="configuration-identity" aria-label="Current configuration identity"><div><strong>Current Model: {displayedModel}</strong><strong>Parameter File: {copy?.filename??'Not loaded'}</strong></div><p>Source: {registered?.inspection.sourceFile??'Unavailable until binary discovery'} · compiled source association; build freshness remains separately reported</p><p>Parameter path: {copy?.hostPath??'Browser import / no trusted Host path'}</p><p className={warning?'pairing-warning':'section-note'}>{sourceModels.state==='pending'?sourceModels.message:warning??'Model / parameter association unconfirmed; filenames do not verify compatibility.'}</p></section>;
}
// Shared by editors and the preview action; stale inspection cannot validate a new Working Copy.
// eslint-disable-next-line react-refresh/only-export-components
export function currentInspection(core:ReturnType<typeof useCoreParameters>,text:string){
 const inspection=core.inspection;
 return inspection&&inspection.text===text&&inspection.response.identity.caseId===core.model&&sameBuildScope(inspection.response.identity,core.configurationScope)?inspection.response.core:undefined;
}
