import {inputs,inspect,gitIdentity,makeManifest,saveManifest,loadManifest,same} from './buildManifest.ts';
import {BuildLog} from './buildLog.ts';
import {StringDecoder} from 'node:string_decoder';
import {spawn} from 'node:child_process';
import type {ChildProcess} from 'node:child_process';
import {randomUUID} from 'node:crypto';
import {CMAKE,validateProfile,profileFingerprint} from './buildProfile.ts';
import type {BuildProfile,BuildSnapshot,BuildResult,InputFingerprint,FileFingerprint} from '../src/host/contracts.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
export class BuildError extends Error { status:number; constructor(message:string,status=400){super(message);this.status=status;} }
// Module-only seam for deterministic negative tests; never exposed to HTTP.
export interface RunnerHooks { cmake?:string; spawn?:typeof spawn }
export class BuildRunner {
 readonly profile:BuildProfile; readonly root:string; readonly projectId:string;
 private hooks:RunnerHooks; private current:BuildSnapshot; private child?:ChildProcess; private log?:BuildLog;
 constructor(root:string,projectId:string,profile:BuildProfile,hooks:RunnerHooks={}){
  this.root=root;this.projectId=projectId;this.profile=structuredClone(profile);this.hooks=hooks;
  this.current={protocolVersion:PROTOCOL_VERSION,projectId,profile:this.profile,configured:false,state:'not-configured',mappingState:profile.caseId?'configured':'unknown',binaryState:'freshness-unknown',freshnessReason:'No successful Studio build manifest.',changedInputs:[]};
 }
 async initialize(){await this.validate();this.current.lastSuccessfulBuild=await loadManifest(this.profile);return this.refreshFreshness();}
 async refreshFreshness(finalizing=false){
  if(this.isActive()&&!finalizing)return this.snapshot();
  const m=this.current.lastSuccessfulBuild;this.current.changedInputs=[];
  try{const binary=await inspect(this.root,this.profile.outputBinaryRelative,true);
   if(!m){this.current.binaryState='freshness-unknown';this.current.freshnessReason='Executable available, but no successful Studio build provenance.';}
   else if(m.buildProfileFingerprint!==profileFingerprint(this.profile)){this.current.binaryState='freshness-unknown';this.current.freshnessReason='Build configuration changed since last successful Build.';}
   else{
    for(const old of m.trackedInputFingerprints){try{if(!same(old.fingerprint,await inspect(this.root,old.relativePath)))this.current.changedInputs.push(old.relativePath);}catch{this.current.changedInputs.push(old.relativePath);}}
    if(this.current.changedInputs.length||!m.inputsStableDuringBuild){this.current.binaryState='needs-build';this.current.freshnessReason=this.current.changedInputs.length?'Tracked build inputs changed since successful Build.':'Tracked inputs changed during Build; build again for a stable snapshot.';}
    else if(!same(binary,m.outputBinary.fingerprint)){this.current.binaryState='freshness-unknown';this.current.freshnessReason='Executable differs from last successful Build manifest.';}
    else{this.current.binaryState=this.profile.dependenciesComplete?'built-from-current-tracked-inputs':'freshness-unknown';this.current.freshnessReason=this.profile.dependenciesComplete?'Explicit tracked inputs match the successful Build.':'Tracked inputs match; full dependency coverage is unknown.';}
   }
  }catch{this.current.binaryState='missing';this.current.freshnessReason='Expected executable missing or unreadable; no readiness claim.';}
  return this.snapshot();
 }
 snapshot(){return structuredClone(this.current);}
 async validate(){try{await validateProfile(this.root,this.profile,this.hooks.cmake??CMAKE);this.current.configured=true;this.current.reason=undefined;if(this.current.state==='not-configured')this.current.state='ready';}catch(e){this.current.configured=false;this.current.reason=e instanceof Error?e.message:'Build profile unavailable';if(!this.current.activeBuildId)this.current.state='not-configured';}return this.snapshot();}
 async start(projectId:string,profileId:string){
  if(projectId!==this.projectId||profileId!==this.profile.id)throw new BuildError('Unknown project or build profile.');
  if(this.current.activeBuildId)throw new BuildError('build-busy',409);
  const id=randomUUID(),startedAt=new Date().toISOString();this.current.activeBuildId=id;this.current.state='queued';
  try{await this.validate();if(!this.current.configured)throw new BuildError(this.current.reason??'Not configured');}
  catch(e){delete this.current.activeBuildId;this.current.state='not-configured';throw e;}
  this.log=new BuildLog(this.projectId,id);this.log.append('state',undefined,'building');this.current.state='building';
  void this.run(id,startedAt);
  return {protocolVersion:PROTOCOL_VERSION,projectId:this.projectId,buildId:id,profileId,startedAt};
 }
 private async run(id:string,startedAt:string){
  const result:BuildResult={projectId:this.projectId,buildId:id,startedAt,finishedAt:'',state:'failed'};
  try{
   const before:InputFingerprint[]=await inputs(this.profile);const preBinary:FileFingerprint|undefined=await inspect(this.root,this.profile.outputBinaryRelative,true).catch(()=>undefined);const git=await gitIdentity(this.root);
   await new Promise<void>((resolve,reject)=>{
    const child=(this.hooks.spawn??spawn)(this.hooks.cmake??CMAKE,['--build',this.root+'/'+this.profile.buildDirRelative,'--target',this.profile.target,'--parallel',String(this.profile.parallelism)],{cwd:this.root,shell:false,env:{PATH:'/usr/local/cuda-12.8/bin:/usr/local/bin:/usr/bin:/bin',HOME:process.env.HOME??'/home/arch',LANG:'C.UTF-8'},stdio:['ignore','pipe','pipe']});this.child=child;
    for(const [stream,kind] of [[child.stdout,'stdout'],[child.stderr,'stderr']] as const){const decoder=new StringDecoder('utf8');stream?.on('data',(chunk:Buffer)=>this.log?.append(kind,decoder.write(chunk)));stream?.on('end',()=>{const tail=decoder.end();if(tail)this.log?.append(kind,tail);});}
    child.once('error',()=>reject(new Error('Build process could not start.')));child.once('close',(code,signal)=>{result.exitCode=code;result.signal=signal;if(code===0)resolve();else reject(new Error('Build exited unsuccessfully.'));});
   });const manifest=await makeManifest(this.profile,this.projectId,id,startedAt,before,preBinary,git);await saveManifest(this.profile,manifest);this.current.lastSuccessfulBuild=manifest;result.state='succeeded';
  }catch(e){result.error=e instanceof Error?e.message:'Build failed';}
  finally{result.finishedAt=new Date().toISOString();if(result.error)this.log?.append('stderr',result.error);this.log?.append('state',undefined,result.state);this.current.latestResult=result;this.current.state=result.state;this.child=undefined;await this.refreshFreshness(true);delete this.current.activeBuildId;}
 }
 events(buildId:string){if(!this.log||this.log.buildId!==buildId)throw new BuildError('Unknown or expired build ID.',404);return this.log.snapshot();}
 isActive(){return !!this.current.activeBuildId;}
 // CLI refuses to abandon a live compiler; no generic cancellation/PID API.
 get processId(){return this.child?.pid;}
}
