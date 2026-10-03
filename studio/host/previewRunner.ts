import {spawn,execFile} from 'node:child_process';
import {PreviewSession,sessionCapability} from './previewSession.ts';
import type {SessionResult,SessionRequest,SessionCapability,SessionProgress} from './previewSession.ts';
import type {ChildProcessWithoutNullStreams} from 'node:child_process';
import {createHash,randomUUID} from 'node:crypto';
import {checkedPath} from './files.ts';
import {inputs,inspect,same} from './buildManifest.ts';
import {profileFingerprint} from './buildProfile.ts';
import {BuildError,BuildRunner} from './buildRunner.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
import {MAX_PREVIEW_BYTES} from '../src/host/previewContracts.ts';
import {samplingForDimension} from '../src/host/previewContracts.ts';
import {profilesFromModels} from './previewProfile.ts';
import type {PreviewStatus,PreviewProfile,RealPreviewRequest,PreviewIdentity} from '../src/host/previewContracts.ts';
import {validateCorePreview,validateModelCapabilities} from '../src/host/previewValidation.ts';
export interface PreviewHooks {spawn?:typeof spawn;timeoutMs?:number;graceMs?:number}
interface PendingPreview {text:string;count:number|number[];identity:PreviewIdentity;profile:PreviewProfile;queuedAt:number}
export class PreviewRunner {
 externalBusy?:()=>boolean;
 readonly build:BuildRunner; profile:PreviewProfile; private profiles:PreviewProfile[];
 private capabilityKey?:string; private capabilityPromise?:Promise<unknown>;
 private closed=false; private capabilityQueries=new Map<AbortController,Promise<void>>();
 private current:PreviewStatus; private child?:ChildProcessWithoutNullStreams; private cancelled=false;
 private session?:PreviewSession; private sessionKey?:string; private sessionGeneration=0;
 private reaping:Promise<void>=Promise.resolve();
 private auxiliaryRequestId?:string;
 private pending?:PendingPreview; private replacedPending=0;
 private hooks:PreviewHooks; private killTimer?:ReturnType<typeof setTimeout>;
 constructor(build:BuildRunner,profile:PreviewProfile,hooks:PreviewHooks={},profiles:PreviewProfile[]=[profile]) {
  this.profiles=structuredClone(profiles);this.build=build;this.profile=structuredClone(profile);this.hooks=hooks;
  this.build.beforeStart=()=>this.endSession('Build starting; retire Preview session.');
  this.current={protocolVersion:PROTOCOL_VERSION,projectId:build.projectId,profile:this.profile,profiles:this.profiles,ready:false,reason:'Build required before real preview.',state:'none'};
 }
 isActive(){return this.current.state==='generating'||!!this.auxiliaryRequestId;}
 snapshot(){return structuredClone(this.current);}
 /** Cache cheap discovery per successful build/binary identity, not per edit. */
 private async capabilities(buildId:string,binarySha256:string):Promise<unknown>{
  if(this.closed)throw new Error('Preview Host is closed.');
  const binary=await checkedPath(this.build.root,this.build.profile.outputBinaryRelative);
  if(this.closed)throw new Error('Preview Host is closed.');
  const key=JSON.stringify([this.build.root,binary,buildId,binarySha256]);
  if(key!==this.capabilityKey||!this.capabilityPromise){
   this.capabilityKey=key;
   const controller=new AbortController();
   let finish!:()=>void;
   this.capabilityQueries.set(controller,new Promise<void>(resolve=>{finish=resolve;}));
   this.capabilityPromise=new Promise<unknown>((resolve,reject)=>{
    const query=execFile(binary,['--preview-capabilities'],{cwd:this.build.root,timeout:3000,killSignal:'SIGKILL',signal:controller.signal,maxBuffer:65536,encoding:'utf8',env:{PATH:'/usr/bin:/bin',LANG:'C.UTF-8',OMP_NUM_THREADS:'1',CUDA_VISIBLE_DEVICES:''}},(error,stdout)=>{
     if(error){reject(new Error('Preview capability process could not start or query failed.'));return;}
     try{const result=JSON.parse(stdout);capabilityExtensions(result);resolve(result);}
     catch(e){reject(e);}
    }).once('close',()=>{this.capabilityQueries.delete(controller);finish();});
    controller.signal.addEventListener('abort',()=>{query.kill('SIGKILL');},{once:true});
   });
   void this.capabilityPromise.catch(()=>{if(this.capabilityKey===key){this.capabilityKey=undefined;this.capabilityPromise=undefined;}});
  }
  return this.capabilityPromise;
 }
 async readiness(){
  const b=await this.build.validate();await this.build.refreshFreshness();const m=this.build.snapshot().lastSuccessfulBuild;
  this.current.ready=false;this.current.build=m;
  try {
   if(this.closed)throw new Error('Preview Host is closed.');
   if(!b.configured||this.build.isActive())throw new Error('Build unavailable or active.');
   if(this.build.snapshot().binaryState==='needs-build')throw new Error('Tracked source or compiler inputs changed. Build required before real preview.');
   if(this.build.profile.id!==this.profile.buildProfileId)throw new Error('Preview profile does not match the configured Build.');
   if(!m||!m.inputsStableDuringBuild||m.buildProfileFingerprint!==profileFingerprint(this.build.profile))throw new Error('Build required before real preview: no matching successful manifest.');
   const now=await inputs(this.build.profile);
   if(now.length!==m.trackedInputFingerprints.length||now.some((v,i)=>v.relativePath!==m.trackedInputFingerprints[i].relativePath||!same(v.fingerprint,m.trackedInputFingerprints[i].fingerprint)))throw new Error('Tracked source changed. Build required before real preview.');
   if(!same(await inspect(this.build.root,this.build.profile.outputBinaryRelative,true),m.outputBinary.fingerprint))throw new Error('Binary differs from successful Build. Build required before real preview.');
   const capabilities=await this.capabilities(m.buildId,m.outputBinary.fingerprint.sha256);
   const root=capabilities as {modelCapabilities?:unknown};
   if(root.modelCapabilities!==undefined){
    const models=validateModelCapabilities(root.modelCapabilities);
    this.profiles=profilesFromModels(models,this.build.profile.id);
    this.current.modelCapabilities=models;this.current.profiles=this.profiles;
    const selected=this.profiles.find(p=>p.id===this.profile.id);
    if(selected){this.profile=selected;this.current.profile=selected;}
   }
   if(this.closed)throw new Error('Preview Host is closed.');
   this.current.ready=true;this.current.reason='Preview uses the last successful tracked build. Full dependency freshness is not independently verified.';
  } catch(e){this.current.reason=e instanceof Error?e.message:'Preview readiness unknown';}
  if(!this.current.ready&&this.session)await this.endSession('Preview Build readiness changed.');
  return this.snapshot();
 }
 async start(r:RealPreviewRequest){
  if(this.closed)throw new BuildError('Preview Host is closed.',409);
  if(this.auxiliaryRequestId||this.externalBusy?.())throw new BuildError('Initialization workflow is active; wait or cancel it.',409);
  const allowed=['projectId','profileId','configText','configRevision','requestedSampleCount','requestedShape'];
  if(Object.keys(r).some(k=>!allowed.includes(k))||r.projectId!==this.build.projectId||typeof r.profileId!=='string'||typeof r.configText!=='string'||!r.configText||Buffer.byteLength(r.configText)>1024*1024||r.configText.includes('\0')||Buffer.from(r.configText,'utf8').toString('utf8')!==r.configText||r.configRevision!==createHash('sha256').update(r.configText).digest('hex'))throw new BuildError('Invalid Preview request or config revision.');
  let profile=this.profiles.find(p=>p.id===r.profileId);
  if(!profile&&!this.isActive()){await this.readiness();profile=this.profiles.find(p=>p.id===r.profileId);}
  if(!profile)throw new BuildError('Unknown Host-owned Preview profile.');
  const chosen=profile;let count:number|number[];
  if(chosen.dimension>1){
   count=r.requestedShape??chosen.defaultShape!;
   if(r.requestedSampleCount!==undefined||!Array.isArray(count)||count.length!==chosen.dimension||count.some(n=>!Number.isInteger(n)||n<2||n>(chosen.maxPerAxis??256))||count.reduce((a,b)=>a*b,1)>chosen.maxSampleCount)throw new BuildError('Invalid bounded multidimensional Preview sampling shape.');
  }else{count=r.requestedSampleCount??chosen.defaultSampleCount;if(r.requestedShape!==undefined||!Number.isInteger(count)||count<2||count>chosen.maxSampleCount)throw new BuildError('Preview samples must be 2..4096.');}
  if(this.isActive()){
   if(!this.session||this.cancelled)throw new BuildError('Preview is already generating; cancel or wait.',409);
   const m=this.current.build;
   if(!this.current.ready||!m)throw new BuildError('Current successful Build required.',409);
   const identity:PreviewIdentity={requestId:randomUUID(),projectId:r.projectId,profileId:r.profileId,caseId:chosen.caseId,configRevision:r.configRevision,buildId:m.buildId,binarySha256:m.outputBinary.fingerprint.sha256};
   if(this.pending)this.replacedPending++;
   this.pending={text:r.configText,count:structuredClone(count),identity,profile:chosen,queuedAt:performance.now()};
   this.current.queue={activeRequestId:this.current.requestId!,pendingRequestId:identity.requestId,replacedPending:this.replacedPending};
   return {protocolVersion:PROTOCOL_VERSION,projectId:r.projectId,requestId:identity.requestId,identity};
  }
  this.profile=chosen;this.current.profile=chosen;
  this.current.state='generating';this.cancelled=false;this.current.error=undefined;this.current.diagnostics=undefined;this.current.failure=undefined;
  const requestId=randomUUID();this.current.requestId=requestId;this.current.queue={activeRequestId:requestId,replacedPending:this.replacedPending};
  try {await this.readiness();if(!this.current.ready)throw new BuildError(this.current.reason,409);}
  catch(e){this.current.state=this.cancelled?'cancelled':'failed';throw e;}
  const m=this.current.build!;
  const identity:PreviewIdentity={requestId,projectId:r.projectId,profileId:r.profileId,caseId:chosen.caseId,configRevision:r.configRevision,buildId:m.buildId,binarySha256:m.outputBinary.fingerprint.sha256};
  void this.run(r.configText,count,identity,performance.now());
  return {protocolVersion:PROTOCOL_VERSION,projectId:r.projectId,requestId,identity};
 }
 /** B commands borrow the same Host-owned worker and exclusion boundary as field Preview. */
 async auxiliary(request:SessionRequest,capability:SessionCapability,expected:{buildId:string;binarySha256:string},progress?:(event:SessionProgress)=>void){
  if(this.isActive())throw new BuildError('Preview is already generating; cancel or wait.',409);
  this.auxiliaryRequestId=request.requestId;
  try{
   const status=await this.readiness();
   if(!status.ready||status.build?.buildId!==expected.buildId||status.build.outputBinary.fingerprint.sha256!==expected.binarySha256)throw new BuildError('Build changed before workflow.',409);
   const binary=await checkedPath(this.build.root,this.build.profile.outputBinaryRelative);
   const key=JSON.stringify([this.build.projectId,this.build.root,binary,expected.binarySha256,expected.buildId,this.build.profile.id]);
   if(this.session&&(this.sessionKey!==key||!this.session.reusable))await this.endSession('Workflow session identity changed or request limit reached.');
   if(this.auxiliaryRequestId!==request.requestId)throw new Error('Workflow cancelled.');
   if(!this.session){
    await this.reaping;
    if(this.auxiliaryRequestId!==request.requestId)throw new Error('Workflow cancelled.');
    this.sessionGeneration++;
    this.session=new PreviewSession({binary,cwd:this.build.root,capability,spawn:this.hooks.spawn,timeoutMs:this.hooks.timeoutMs,graceMs:this.hooks.graceMs});
    this.sessionKey=key;
   }
   const session=this.session,generation=this.sessionGeneration;
   const result=await session.request(request,progress);
   if(this.session!==session||generation!==this.sessionGeneration||this.auxiliaryRequestId!==request.requestId)throw new Error('Obsolete workflow session result.');
   const after=await this.readiness();
   if(!after.ready||after.build?.buildId!==expected.buildId||after.build.outputBinary.fingerprint.sha256!==expected.binarySha256)throw new Error('Build changed while inspecting.');
   return result;
  }finally{if(this.auxiliaryRequestId===request.requestId)this.auxiliaryRequestId=undefined;}
 }
 async cancelAuxiliary(requestId:string){
  if(this.auxiliaryRequestId!==requestId)throw new BuildError('No matching workflow.',409);
  this.auxiliaryRequestId=undefined;
  await this.endSession('Initialization workflow cancelled.');
 }
 private terminate(){
  const child=this.child;if(!child?.pid||this.killTimer)return;
  const kill=(signal:NodeJS.Signals)=>{try{process.kill(-child.pid!,signal);}catch{child.kill(signal);}};
  kill('SIGTERM');this.killTimer=setTimeout(()=>kill('SIGKILL'),this.hooks.graceMs??1000);
 }
 cancel(requestId:string){
  if((requestId!==this.current.requestId&&requestId!==this.pending?.identity.requestId)||!this.isActive())throw new BuildError('No matching active Preview.',409);
  this.pending=undefined;this.current.queue=undefined;this.cancelled=true;this.terminate();void this.endSession('Preview cancelled.');return this.snapshot();
 }
 private async endSession(reason:string){
  const session=this.session;this.session=undefined;this.sessionKey=undefined;
  if(session){this.sessionGeneration++;this.reaping=session.terminate(reason);}
  await this.reaping;
 }
 async shutdown(){this.closed=true;this.current.ready=false;this.current.reason='Preview Host is closed.';const queries=[...this.capabilityQueries.entries()];for(const [controller] of queries)controller.abort();await Promise.allSettled(queries.map(([,closed])=>closed));this.capabilityKey=undefined;this.capabilityPromise=undefined;this.auxiliaryRequestId=undefined;this.pending=undefined;this.current.queue=undefined;this.cancelled=true;this.terminate();await this.endSession('Preview Host shutdown.');}
 private async run(text:string,count:number|number[],identity:PreviewIdentity,queuedAt:number){
  const startedAt=performance.now();
  this.current.timing={hostQueueMilliseconds:startedAt-queuedAt};

  let timer:ReturnType<typeof setTimeout>|undefined;
  try {
   await this.readiness();
   if(!this.current.ready||this.current.build?.buildId!==identity.buildId||this.current.build.outputBinary.fingerprint.sha256!==identity.binarySha256)throw new Error('Build changed before queued Preview.');
   const binary=await checkedPath(this.build.root,this.build.profile.outputBinaryRelative);
   if(this.cancelled)throw new Error('Preview cancelled.');
   const capabilities=await this.capabilities(identity.buildId,identity.binarySha256);
   if(this.cancelled)throw new Error('Preview cancelled.');
   const extensions=capabilityExtensions(capabilities);
   const caps=capabilities as {modelCapabilities?:unknown};
   const models=caps.modelCapabilities===undefined?undefined:validateModelCapabilities(caps.modelCapabilities);
   this.current.modelCapabilities=models;
   const model=models?.find(m=>m.caseId===identity.caseId);
   if(identity.caseId==='CellularDet'&&!model)throw new Error('CellularDet 2D model capability unavailable.');
   if(model){const shape=Array.isArray(count)?count:[count],budget=samplingForDimension(model,shape.length);if(!budget||!model.dimensions.includes(shape.length)||shape.some(n=>n<budget.minPerAxis||n>budget.maxPerAxis)||shape.reduce((a,b)=>a*b,1)>budget.maxTotalSamples)throw new Error('Requested sampling exceeds model capabilities.');}
   else if(identity.caseId!=='Sod')throw new Error('Selected model capability unavailable.');
   const samplingArgs=Array.isArray(count)?[...count].reverse().flatMap((n,axis)=>['--samples-x'+(axis+1),String(n)]):['--samples',String(count)];
   let output:{bytes:Buffer;code:number|null};
   let sessionResult:SessionResult|undefined;
   const capability=sessionCapability(capabilities);
   if(capability){
    const key=JSON.stringify([this.build.projectId,this.build.root,binary,identity.binarySha256,identity.buildId,this.build.profile.id]);
    if(this.session&&(this.sessionKey!==key||!this.session.reusable))await this.endSession('Preview session identity changed or request limit reached.');
    if(this.cancelled)throw new Error('Preview cancelled.');
    if(!this.session){
     await this.reaping;
     if(this.cancelled)throw new Error('Preview cancelled.');
     this.sessionGeneration++;
     this.session=new PreviewSession({binary,cwd:this.build.root,capability,spawn:this.hooks.spawn,timeoutMs:this.hooks.timeoutMs,graceMs:this.hooks.graceMs});
     this.sessionKey=key;
    }
    const session=this.session,generation=this.sessionGeneration;
    this.child=undefined;
    this.current.session={generation,processToken:session.processToken,stage:'request'};
    sessionResult=await session.request({command:'--preview',caseId:identity.caseId,requestId:identity.requestId,configText:text,
     ...(Array.isArray(count)?Object.fromEntries([...count].reverse().map((n,axis)=>['samplesX'+(axis+1),n])):{samples:count})},event=>{
      if(this.session===session&&this.sessionGeneration===generation&&!this.cancelled)
       this.current.session={generation,processToken:session.processToken,...event};
     });
    if(this.session!==session||generation!==this.sessionGeneration||this.cancelled)throw new Error('Obsolete Preview session result.');
    this.current.session={generation,processToken:session.processToken,stage:'complete',
     elapsedMilliseconds:sessionResult.elapsedMilliseconds,sequence:sessionResult.sequence,
     transportParseMilliseconds:sessionResult.transportParseMilliseconds,resources:sessionResult.resources,stages:sessionResult.stages};
    output={bytes:Buffer.from(JSON.stringify(sessionResult.response)),code:sessionResult.exitCode};
   }else{
    await this.endSession('Binary uses single-shot Preview.');
    this.current.session=undefined;
    output=await new Promise<{bytes:Buffer;code:number|null}>((resolve,reject)=>{
    const child=(this.hooks.spawn??spawn)(binary,['--preview',identity.caseId,'--config-stdin',...samplingArgs,'--request-id',identity.requestId],{cwd:this.build.root,shell:false,detached:true,env:{PATH:'/usr/bin:/bin',HOME:process.env.HOME??'/home/arch',LANG:'C.UTF-8',OMP_NUM_THREADS:'1',CUDA_VISIBLE_DEVICES:''},stdio:['pipe','pipe','pipe']});this.child=child;
    let size=0,stderr=0,failure='';const chunks:Buffer[]=[];
    timer=setTimeout(()=>{failure='Preview timed out.';this.terminate();},this.hooks.timeoutMs??120000);
    child.stdout.on('data',(b:Buffer)=>{size+=b.length;if(size>MAX_PREVIEW_BYTES){failure='Preview response exceeds 8 MiB.';this.terminate();}else chunks.push(b);});
    child.stderr.on('data',(b:Buffer)=>{stderr+=b.length;if(stderr>65536){failure='Preview diagnostics exceed limit.';this.terminate();}});
    child.stdin.on('error',()=>{/* close/error determines the result; EPIPE never becomes a UI crash */});
    child.once('error',()=>reject(new Error('Preview process could not start.')));
    child.once('close',(code)=>{if(failure)reject(new Error(failure));else resolve({bytes:Buffer.concat(chunks),code});});
    child.stdin.end(text,'utf8');
   });
   }
   if(this.cancelled)throw new Error('Preview cancelled.');
   if(output.bytes.length>MAX_PREVIEW_BYTES)throw new Error('Preview response exceeds 8 MiB.');
   const core=validateCorePreview(JSON.parse(new TextDecoder('utf8',{fatal:true}).decode(output.bytes)),identity,count);
   if(model&&core.data){
    const geometry=core.data.coordinates?.geometry??(core.state?.grid as {geometry?:string}|undefined)?.geometry??'cartesian';
    if(!model.dimensions.includes(core.data.dimension)||!model.geometries.includes(geometry))throw new Error('Response is outside negotiated model domain.');
   }
   if(model&&core.data&&(core.data.fields.length>model.maxFields||core.data.fields.some(f=>!model.fields.includes(f.key))||output.bytes.length>model.maxResponseBytes))throw new Error('Response exceeds model field/byte capabilities.');
   if(core.status==='error')this.current.failure=core;
   if(identity.caseId!=='Sod'||!extensions.metadata)delete core.parameterMetadata;
   if(identity.caseId!=='Sod'||!extensions.binding)delete core.graphicalBindings;
   this.current.diagnostics=core.diagnostics;
   if(output.code!==0||core.status!=='ok')throw new Error(core.diagnostics.find(d=>d.severity==='error')?.message??'Preview exited unsuccessfully.');
   await this.readiness();
   if(this.cancelled)throw new Error('Preview cancelled.');
   if(!this.current.ready||this.current.build?.buildId!==identity.buildId||this.current.build.outputBinary.fingerprint.sha256!==identity.binarySha256)throw new Error('Build changed while generating; result discarded.');
   if(!this.pending){
    this.current.timing={hostQueueMilliseconds:startedAt-queuedAt,hostElapsedMilliseconds:performance.now()-startedAt,
     coreElapsedMilliseconds:sessionResult?.elapsedMilliseconds,transportParseMilliseconds:sessionResult?.transportParseMilliseconds};
    this.current.result={protocolVersion:PROTOCOL_VERSION,identity,generatedAt:new Date().toISOString(),core};this.current.state='succeeded';
   }
  } catch(e){this.current.state=this.cancelled?'cancelled':'failed';this.current.error=this.cancelled?'Preview cancelled.':e instanceof Error?e.message:'Preview failed.';}
  finally {
   if(timer)clearTimeout(timer);if(this.killTimer)clearTimeout(this.killTimer);this.killTimer=undefined;this.child=undefined;
   const pending=this.pending;this.pending=undefined;
   if(pending&&!this.cancelled){
    this.profile=pending.profile;this.current.profile=pending.profile;this.current.requestId=pending.identity.requestId;
    this.current.state='generating';this.current.error=undefined;this.current.failure=undefined;this.current.diagnostics=undefined;
    this.current.queue={activeRequestId:pending.identity.requestId,replacedPending:this.replacedPending};
    void this.run(pending.text,pending.count,pending.identity,pending.queuedAt);
   }else this.current.queue=undefined;
  }
 }
}

function capabilityExtensions(v:unknown){
 const o=v as {schemaVersion?:string;kind?:string;cases?:unknown[];dimensions?:unknown[];extensions?:{parameterMetadata?:{version?:string;cases?:{caseId?:string;keys?:string[]}[]};graphicalBindings?:{version?:string;cases?:{caseId?:string;ids?:string[]}[]}}};
 if(!o||o.schemaVersion!=='1.0'||o.kind!=='preview-capabilities'||!Array.isArray(o.cases)||!o.cases.includes('Sod')||!Array.isArray(o.dimensions)||!o.dimensions.includes(1))throw new Error('Sod 1D capability unavailable.');
 const m=o.extensions?.parameterMetadata,b=o.extensions?.graphicalBindings;
 const metadata=m?.version==='1'&&Array.isArray(m.cases)&&m.cases.some(c=>c.caseId==='Sod'&&Array.isArray(c.keys)&&c.keys.includes('x_pos'));
 const binding=metadata&&b?.version==='1'&&Array.isArray(b.cases)&&b.cases.some(c=>c.caseId==='Sod'&&Array.isArray(c.ids)&&c.ids.includes('Sod.x_pos'));
 return {metadata,binding};
}
