import {spawn} from 'node:child_process';
import type {ChildProcessWithoutNullStreams} from 'node:child_process';
import {createHash,randomUUID} from 'node:crypto';
import {checkedPath} from './files.ts';
import {inputs,inspect,same} from './buildManifest.ts';
import {profileFingerprint} from './buildProfile.ts';
import {BuildError,BuildRunner} from './buildRunner.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
import {MAX_PREVIEW_BYTES} from '../src/host/previewContracts.ts';
import type {PreviewStatus,PreviewProfile,RealPreviewRequest,PreviewIdentity} from '../src/host/previewContracts.ts';
import {validateCorePreview} from '../src/host/previewValidation.ts';
export interface PreviewHooks {spawn?:typeof spawn;timeoutMs?:number;graceMs?:number}
export class PreviewRunner {
 readonly build:BuildRunner; readonly profile:PreviewProfile;
 private current:PreviewStatus; private child?:ChildProcessWithoutNullStreams; private cancelled=false;
 private hooks:PreviewHooks; private killTimer?:ReturnType<typeof setTimeout>;
 constructor(build:BuildRunner,profile:PreviewProfile,hooks:PreviewHooks={}) {
  this.build=build;this.profile=structuredClone(profile);this.hooks=hooks;
  this.current={protocolVersion:PROTOCOL_VERSION,projectId:build.projectId,profile:this.profile,ready:false,reason:'Build required before real preview.',state:'none'};
 }
 isActive(){return this.current.state==='generating';}
 snapshot(){return structuredClone(this.current);}
 async readiness(){
  const b=await this.build.validate();await this.build.refreshFreshness();const m=this.build.snapshot().lastSuccessfulBuild;
  this.current.ready=false;this.current.build=m;
  try {
   if(!b.configured||this.build.isActive())throw new Error('Build unavailable or active.');
   if(this.build.profile.id!==this.profile.buildProfileId||this.build.profile.caseId!==this.profile.caseId)throw new Error('Preview profile does not match the configured Build.');
   if(!m||!m.inputsStableDuringBuild||m.buildProfileFingerprint!==profileFingerprint(this.build.profile))throw new Error('Build required before real preview: no matching successful manifest.');
   const now=await inputs(this.build.profile);
   if(now.length!==m.trackedInputFingerprints.length||now.some((v,i)=>v.relativePath!==m.trackedInputFingerprints[i].relativePath||!same(v.fingerprint,m.trackedInputFingerprints[i].fingerprint)))throw new Error('Tracked source changed. Build required before real preview.');
   if(!same(await inspect(this.build.root,this.build.profile.outputBinaryRelative,true),m.outputBinary.fingerprint))throw new Error('Binary differs from successful Build. Build required before real preview.');
   this.current.ready=true;this.current.reason='Preview uses the last successful tracked build. Full dependency freshness is not independently verified.';
  } catch(e){this.current.reason=e instanceof Error?e.message:'Preview readiness unknown';}
  return this.snapshot();
 }
 async start(r:RealPreviewRequest){
  if(this.isActive())throw new BuildError('Preview is already generating; cancel or wait.',409);
  const allowed=['projectId','profileId','configText','configRevision','requestedSampleCount'];
  if(Object.keys(r).some(k=>!allowed.includes(k))||r.projectId!==this.build.projectId||r.profileId!==this.profile.id||typeof r.configText!=='string'||!r.configText||Buffer.byteLength(r.configText)>1024*1024||r.configText.includes('\0')||Buffer.from(r.configText,'utf8').toString('utf8')!==r.configText||r.configRevision!==createHash('sha256').update(r.configText).digest('hex'))throw new BuildError('Invalid Preview request or config revision.');
  const count=r.requestedSampleCount??512;
  if(!Number.isInteger(count)||count<2||count>4096)throw new BuildError('Preview samples must be 2..4096.');
  this.current.state='generating';this.cancelled=false;this.current.error=undefined;this.current.diagnostics=undefined;
  const requestId=randomUUID();this.current.requestId=requestId;
  try {await this.readiness();if(!this.current.ready)throw new BuildError(this.current.reason,409);}
  catch(e){this.current.state=this.cancelled?'cancelled':'failed';throw e;}
  const m=this.current.build!;
  const identity:PreviewIdentity={requestId,projectId:r.projectId,profileId:r.profileId,caseId:'Sod',configRevision:r.configRevision,buildId:m.buildId,binarySha256:m.outputBinary.fingerprint.sha256};
  void this.run(r.configText,count,identity);
  return {protocolVersion:PROTOCOL_VERSION,projectId:r.projectId,requestId,identity};
 }
 private terminate(){
  const child=this.child;if(!child?.pid||this.killTimer)return;
  const kill=(signal:NodeJS.Signals)=>{try{process.kill(-child.pid!,signal);}catch{child.kill(signal);}};
  kill('SIGTERM');this.killTimer=setTimeout(()=>kill('SIGKILL'),this.hooks.graceMs??1000);
 }
 cancel(requestId:string){
  if(requestId!==this.current.requestId||!this.isActive())throw new BuildError('No matching active Preview.',409);
  this.cancelled=true;this.terminate();return this.snapshot();
 }
 async shutdown(){if(this.isActive()){this.cancelled=true;this.terminate();}}
 private async run(text:string,count:number,identity:PreviewIdentity){
  let timer:ReturnType<typeof setTimeout>|undefined;
  try {
   const binary=await checkedPath(this.build.root,this.build.profile.outputBinaryRelative);
   if(this.cancelled)throw new Error('Preview cancelled.');
   const output=await new Promise<{bytes:Buffer;code:number|null}>((resolve,reject)=>{
    const child=(this.hooks.spawn??spawn)(binary,['--preview','Sod','--config-stdin','--samples',String(count),'--request-id',identity.requestId],{cwd:this.build.root,shell:false,detached:true,env:{PATH:'/usr/bin:/bin',HOME:process.env.HOME??'/home/arch',LANG:'C.UTF-8',OMP_NUM_THREADS:'1',CUDA_VISIBLE_DEVICES:''},stdio:['pipe','pipe','pipe']});this.child=child;
    let size=0,stderr=0,failure='';const chunks:Buffer[]=[];
    timer=setTimeout(()=>{failure='Preview timed out.';this.terminate();},this.hooks.timeoutMs??30000);
    child.stdout.on('data',(b:Buffer)=>{size+=b.length;if(size>MAX_PREVIEW_BYTES){failure='Preview response exceeds 8 MiB.';this.terminate();}else chunks.push(b);});
    child.stderr.on('data',(b:Buffer)=>{stderr+=b.length;if(stderr>65536){failure='Preview diagnostics exceed limit.';this.terminate();}});
    child.stdin.on('error',()=>{/* close/error determines the result; EPIPE never becomes a UI crash */});
    child.once('error',()=>reject(new Error('Preview process could not start.')));
    child.once('close',(code)=>{if(failure)reject(new Error(failure));else resolve({bytes:Buffer.concat(chunks),code});});
    child.stdin.end(text,'utf8');
   });
   if(this.cancelled)throw new Error('Preview cancelled.');
   const core=validateCorePreview(JSON.parse(new TextDecoder('utf8',{fatal:true}).decode(output.bytes)),identity,count);
   this.current.diagnostics=core.diagnostics;
   if(output.code!==0||core.status!=='ok')throw new Error(core.diagnostics.find(d=>d.severity==='error')?.message??'Preview exited unsuccessfully.');
   await this.readiness();
   if(this.cancelled)throw new Error('Preview cancelled.');
   if(!this.current.ready||this.current.build?.buildId!==identity.buildId||this.current.build.outputBinary.fingerprint.sha256!==identity.binarySha256)throw new Error('Build changed while generating; result discarded.');
   this.current.result={protocolVersion:PROTOCOL_VERSION,identity,generatedAt:new Date().toISOString(),core};this.current.state='succeeded';
  } catch(e){this.current.state=this.cancelled?'cancelled':'failed';this.current.error=this.cancelled?'Preview cancelled.':e instanceof Error?e.message:'Preview failed.';}
  finally {if(timer)clearTimeout(timer);if(this.killTimer)clearTimeout(this.killTimer);this.killTimer=undefined;this.child=undefined;}
 }
}
