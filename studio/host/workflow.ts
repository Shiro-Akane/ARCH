import {execFile} from 'node:child_process';
import {createHash,randomUUID} from 'node:crypto';
import {BuildError} from './buildRunner.ts';
import {checkedPath} from './files.ts';
import {sessionCapability} from './previewSession.ts';
import type {SessionCapability,SessionRequest} from './previewSession.ts';
import type {PreviewRunner} from './previewRunner.ts';
import {record} from '../src/host/previewValidation.ts';
import {validateRegistry,validateWorkflowCore} from '../src/host/workflowValidation.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
import type {DiscoveryResponse,WorkflowRequest,WorkflowStatus} from '../src/host/workflowContracts.ts';
/** Fixed semantic operations only; browser never supplies argv, cwd, env or executable. */
export class WorkflowRunner {
 assertCase?:(caseId:string)=>Promise<void>;
 private preview:PreviewRunner;
 private cached?:{discovery:DiscoveryResponse;session:SessionCapability};
 private current:WorkflowStatus;
 private epoch=0;
 constructor(preview:PreviewRunner){this.preview=preview;this.current={protocolVersion:PROTOCOL_VERSION,projectId:preview.build.projectId,state:'none'};}
 snapshot(){return structuredClone(this.current);}
 isActive(){return this.current.state==='running';}
 private async ready(){
  const s=await this.preview.readiness();
  if(!s.ready||!s.build)throw new BuildError(s.reason??'Current successful Build required.',409);
  return {projectId:this.preview.build.projectId,buildId:s.build.buildId,binarySha256:s.build.outputBinary.fingerprint.sha256};
 }
 private async query(command:'--list-cases'|'--preview-capabilities'){
  const b=this.preview.build,binary=await checkedPath(b.root,b.profile.outputBinaryRelative);
  return new Promise<unknown>((resolve,reject)=>execFile(binary,[command],{cwd:b.root,shell:false,encoding:'utf8',timeout:10000,maxBuffer:8*1024*1024,env:{PATH:'/usr/bin:/bin',LANG:'C.UTF-8',OMP_NUM_THREADS:'1',CUDA_VISIBLE_DEVICES:''}},(error,stdout)=>{
   if(error){reject(new BuildError('Binary discovery failed or exceeded its limits.',502));return;}
   try{resolve(JSON.parse(stdout));}catch{reject(new BuildError('Malformed binary discovery response.',502));}
  }));
 }
 async discovery():Promise<DiscoveryResponse>{
  const before=await this.ready();
  const old=this.cached;
  if(old&&old.discovery.buildId===before.buildId&&old.discovery.binarySha256===before.binarySha256)return structuredClone(old.discovery);
  const cases=validateRegistry(await this.query('--list-cases'));
  const caps=await this.query('--preview-capabilities');
  if(!record(caps)||caps.schemaVersion!=='1.0'||caps.kind!=='preview-capabilities'||caps.status!=='ok'||!record(caps.extensions)||!record(caps.extensions.discovery)||caps.extensions.discovery.version!=='1')throw new BuildError('Binary discovery extension unavailable.');
  const session=sessionCapability(caps);
  if(!session)throw new BuildError('Initialization workflow requires the Core session contract.');
  const ext=caps.extensions.amr;let amr:DiscoveryResponse['amr']=null;
  if(ext!==undefined){
   if(!record(ext)||ext.version!=='1'||ext.workerPlatform!=='linux'||ext.meshCommand!=='--preview-amr'||ext.resourcesCommand!=='--amr-resources'||!Array.isArray(ext.cases)||!ext.cases.every(x=>typeof x==='string')||!Array.isArray(ext.geometries)||!ext.geometries.every(x=>typeof x==='string')||!Number.isInteger(ext.defaultMaxBlocks)||Number(ext.defaultMaxBlocks)<1||Number(ext.defaultMaxBlocks)>1024||!Number.isInteger(ext.defaultMemoryMiB)||Number(ext.defaultMemoryMiB)<16||Number(ext.defaultMemoryMiB)>256)throw new BuildError('Unsupported AMR capability.');
   amr={cases:ext.cases as string[],geometries:ext.geometries as string[],defaultMaxBlocks:Number(ext.defaultMaxBlocks),defaultMemoryMiB:Number(ext.defaultMemoryMiB)};
  }
  if(!Array.isArray(caps.modelCapabilities))throw new BuildError('Field model capabilities unavailable.');
  const fieldModels=caps.modelCapabilities.map(m=>{
   if(!record(m)||typeof m.caseId!=='string'||!Array.isArray(m.dimensions)||!m.dimensions.every(n=>Number.isInteger(n)&&Number(n)>=1&&Number(n)<=3)||!Array.isArray(m.geometries)||!m.geometries.every(x=>typeof x==='string'))throw new BuildError('Invalid field model capability.');
   return {caseId:m.caseId,dimensions:m.dimensions as number[],geometries:m.geometries as string[]};
  });
  const after=await this.ready();
  if(before.buildId!==after.buildId||before.binarySha256!==after.binarySha256)throw new BuildError('Build changed during discovery.',409);
  const discovery={...before,protocolVersion:PROTOCOL_VERSION,cases,fieldModels,amr};
  this.cached={discovery,session};return structuredClone(discovery);
 }
 async start(r:WorkflowRequest){
  const allowed=['projectId','caseId','configText','configRevision','operation','meshMaxBlocks','meshMemoryMiB'];
  if(Object.keys(r).some(k=>!allowed.includes(k))||r.projectId!==this.preview.build.projectId||typeof r.caseId!=='string'||!['inspect-case','amr-resources','preview-amr'].includes(r.operation)||typeof r.configText!=='string'||!r.configText||r.configText.includes('\0')||Buffer.byteLength(r.configText)>1024*1024||Buffer.from(r.configText).toString('utf8')!==r.configText||r.configRevision!==createHash('sha256').update(r.configText).digest('hex'))throw new BuildError('Invalid initialization workflow request.');
  if(r.operation!=='preview-amr'&&(r.meshMaxBlocks!==undefined||r.meshMemoryMiB!==undefined))throw new BuildError('Mesh budgets are only valid for AMR Preview.');
  if(this.isActive()||this.preview.isActive())throw new BuildError('Preview is already generating; cancel or wait.',409);
  // Claim before asynchronous capability/readiness checks so concurrent POSTs cannot race.
  const token=++this.epoch,requestId=randomUUID();
  this.current={...this.current,state:'running',requestId,operation:r.operation,stage:'preparing',error:undefined,failure:undefined};
  try{
   await this.assertCase?.(r.caseId);
   const d=await this.discovery(),model=d.cases.find(c=>c.caseId===r.caseId);
   if(!model)throw new BuildError('Case is not registered in the selected binary.');
   if(r.operation==='inspect-case'&&!model.inspection.setupReads)throw new BuildError('Case initialization inspection unavailable.');
   if(r.operation==='preview-amr'&&(!model.initialAmrPreview||!d.amr?.cases.includes(r.caseId)))throw new BuildError('Initial AMR Preview unavailable for this registered case.');
   const max=r.meshMaxBlocks??d.amr?.defaultMaxBlocks,mem=r.meshMemoryMiB??d.amr?.defaultMemoryMiB;
   if(r.operation==='preview-amr'&&(!Number.isInteger(max)||max!<1||max!>1024||!Number.isInteger(mem)||mem!<16||mem!>256))throw new BuildError('Mesh budget outside Core v1 bounds (1–1024 blocks, 16–256 MiB).');
   if(token!==this.epoch)throw new BuildError('Workflow cancelled.',409);
   const identity={...d,caseId:r.caseId,configRevision:r.configRevision,requestId};
   const request:SessionRequest={command:('--'+r.operation) as SessionRequest['command'],caseId:r.caseId,requestId,configText:r.configText,...(r.operation==='preview-amr'?{meshMaxBlocks:max,meshMemoryMiB:mem}:{})};
   void this.finish(token,request,r.operation,identity,this.cached!.session);
   return {protocolVersion:PROTOCOL_VERSION,identity:{projectId:d.projectId,buildId:d.buildId,binarySha256:d.binarySha256,caseId:r.caseId,configRevision:r.configRevision,requestId}};
  }catch(e){if(token===this.epoch){this.current.state='failed';this.current.error=e instanceof Error?e.message:'Workflow unavailable.';}throw e;}
 }
 private async finish(token:number,request:SessionRequest,operation:WorkflowRequest['operation'],scope:{projectId:string;buildId:string;binarySha256:string;caseId:string;configRevision:string;requestId:string},capability:SessionCapability){
  try{
   const outer=await this.preview.auxiliary(request,capability,scope,event=>{if(token===this.epoch)this.current.stage=event.stage;});
   if(token!==this.epoch)return;
   const core=validateWorkflowCore(outer.response,operation,{requestId:scope.requestId,caseId:scope.caseId,configRevision:scope.configRevision});
   if(core.status==='error'){this.current.failure=core;throw new Error(core.diagnostics.filter(d=>d.severity==='error').map(d=>d.message).join('; ')||'Core workflow failed.');}
   if(outer.exitCode!==0)throw new Error('Core workflow exited unsuccessfully.');
   this.current.state='succeeded';this.current.stage='complete';this.current.result={identity:{projectId:scope.projectId,buildId:scope.buildId,binarySha256:scope.binarySha256,caseId:scope.caseId,configRevision:scope.configRevision,requestId:scope.requestId},operation,core};
  }catch(e){if(token===this.epoch){this.current.state='failed';this.current.error=e instanceof Error?e.message:'Workflow failed; previous result retained.';}}
 }
 async cancel(requestId:string){
  if(!this.isActive()||this.current.requestId!==requestId)throw new BuildError('No matching initialization workflow.',409);
  ++this.epoch;
  // Cancellation during discovery has no worker yet; the epoch rejects its later completion.
  try{await this.preview.cancelAuxiliary(requestId);}catch(e){if(!(e instanceof BuildError&&e.status===409))throw e;}
  this.current.state='cancelled';this.current.stage=undefined;this.current.error='Cancelled; previous result retained.';
  return this.snapshot();
 }
}
