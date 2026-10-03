import {mkdir,stat} from 'node:fs/promises';
import {checkedPath} from './files.ts';
import {readBuildToolchainEvidence} from './cmakeEvidence.ts';
import {fingerprintLinkDependencies,changedLinkInputs} from './linkDependencies.ts';
import {fingerprintNinjaDependencies,sameCompilerInputs} from './ninjaDependencies.ts';
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
    let compilerUnknown=false;
    if(this.profile.compilerDependencyMode==='ninja'&&m.compilerInputs){
     try{
      const now=await fingerprintNinjaDependencies(this.root+'/'+this.profile.buildDirRelative);
      if(!sameCompilerInputs(m.compilerInputs,now)){
       const previous=new Map(m.compilerInputs.files.map(f=>[f.path,f]));
       for(const file of now.files){const old=previous.get(file.path);if(!old||old.sha256!==file.sha256||old.size!==file.size)this.current.changedInputs.push(file.path);previous.delete(file.path);}
       this.current.changedInputs.push(...previous.keys());
       if(!this.current.changedInputs.length)this.current.changedInputs.push('compiler dependency graph');
      }
     }catch{
      compilerUnknown=true;
     }
    }
    let toolchainUnknown=false;
    if(this.profile.compilerDependencyMode==='ninja'){
     if(m.compilerDriversStableDuringBuild===undefined)toolchainUnknown=true;
     if(!m.compilerDrivers?.length||m.compilerDriverError)toolchainUnknown=true;
     else try{
      const now=(await readBuildToolchainEvidence(this.root+'/'+this.profile.buildDirRelative)).compilers;
      const previous=new Map(m.compilerDrivers.map(c=>[c.language,c]));
      for(const driver of now){
       const old=previous.get(driver.language);
       if(!old||old.path!==driver.path||old.resolvedPath!==driver.resolvedPath||old.sha256!==driver.sha256||
          old.size!==driver.size||old.id!==driver.id||old.version!==driver.version)
        this.current.changedInputs.push(driver.path);
       if(driver.id==='GNU'){
        if(!old?.components?.length||!old.specsSha256||!driver.components?.length)toolchainUnknown=true;
        else{
         if(old.specsSha256!==driver.specsSha256)this.current.changedInputs.push(driver.path+' specs');
         const prior=new Map(old.components.map(c=>[c.role,c]));
         for(const component of driver.components){
          const saved=prior.get(component.role);
          if(!saved||saved.path!==component.path||saved.resolvedPath!==component.resolvedPath||saved.sha256!==component.sha256||saved.size!==component.size)this.current.changedInputs.push(component.path);
          prior.delete(component.role);
         }
         for(const removed of prior.values())this.current.changedInputs.push(removed.path);
        }
       }
       previous.delete(driver.language);
      }
      for(const old of previous.values())this.current.changedInputs.push(old.path);
     }catch{toolchainUnknown=true;}
    }
    let linkUnknown=false,missingLinkInputs=0;
    if(this.profile.linkDependencyFile){
     if(!m.linkInputs||m.linkInputError)linkUnknown=true;
     else try{
      const now=await fingerprintLinkDependencies(this.root+'/'+this.profile.buildDirRelative,this.profile.linkDependencyFile,this.root+'/'+this.profile.outputBinaryRelative);
      this.current.changedInputs.push(...changedLinkInputs(m.linkInputs,now));
      missingLinkInputs=now.unavailable.length;linkUnknown=missingLinkInputs>0;
     }catch{linkUnknown=true;}
    }
    if(this.current.changedInputs.length||!m.inputsStableDuringBuild||m.compilerDriversStableDuringBuild===false){this.current.binaryState='needs-build';this.current.freshnessReason=this.current.changedInputs.length?'Tracked build inputs changed since successful Build.':'Tracked inputs or compiler toolchain changed during Build; build again for a stable snapshot.';}
    else if(compilerUnknown){this.current.binaryState='freshness-unknown';this.current.freshnessReason='Compiler dependency evidence unavailable or stale.';}
    else if(toolchainUnknown){this.current.binaryState='freshness-unknown';this.current.freshnessReason='Compiler toolchain identity is incomplete or unavailable.';}
    else if(linkUnknown){this.current.binaryState='freshness-unknown';this.current.freshnessReason=missingLinkInputs?('Linker input evidence is incomplete: '+missingLinkInputs+' recorded inputs are missing. Full dependency freshness is unknown.'):'Linker input evidence is incomplete or unavailable.';}
    else if(m.compilerInputsStableDuringBuild===false){this.current.binaryState='freshness-unknown';this.current.freshnessReason='Compiler input stability was not established across Build.';}
    else if(!same(binary,m.outputBinary.fingerprint)){this.current.binaryState='freshness-unknown';this.current.freshnessReason='Executable differs from last successful Build manifest.';}
    else{this.current.binaryState=this.profile.dependenciesComplete?'built-from-current-tracked-inputs':'freshness-unknown';this.current.freshnessReason=this.profile.dependenciesComplete?'Explicit tracked inputs match the successful Build.':'Tracked inputs match; full dependency coverage is unknown.';}
   }
  }catch{this.current.binaryState='missing';this.current.freshnessReason='Expected executable missing or unreadable; no readiness claim.';}
  return this.snapshot();
 }
 snapshot(){return structuredClone(this.current);}
 async validate(){try{await validateProfile(this.root,this.profile,this.hooks.cmake??CMAKE);this.current.configured=true;this.current.reason=undefined;if(this.current.state==='not-configured')this.current.state='ready';}catch(e){this.current.configured=false;this.current.reason=e instanceof Error?e.message:'Build profile unavailable';if(!this.current.activeBuildId)this.current.state='not-configured';}return this.snapshot();}
 executionBlocked?:()=>boolean;
 beforeStart?:()=>Promise<void>;
 async start(projectId:string,profileId:string){
  if(this.executionBlocked?.())throw new BuildError('Preview is active; cancel or wait before Build.',409);
  if(projectId!==this.projectId||profileId!==this.profile.id)throw new BuildError('Unknown project or build profile.');
  if(this.current.activeBuildId)throw new BuildError('build-busy',409);
  const id=randomUUID(),startedAt=new Date().toISOString();this.current.activeBuildId=id;this.current.state='queued';
  try{await this.beforeStart?.();await this.validate();if(!this.current.configured)throw new BuildError(this.current.reason??'Not configured');}
  catch(e){delete this.current.activeBuildId;this.current.state='not-configured';throw e;}
  this.log=new BuildLog(this.projectId,id);this.log.append('state',undefined,'building');this.current.state='building';
  void this.run(id,startedAt);
  return {protocolVersion:PROTOCOL_VERSION,projectId:this.projectId,buildId:id,profileId,startedAt};
 }
 private async run(id:string,startedAt:string){
  const result:BuildResult={projectId:this.projectId,buildId:id,startedAt,finishedAt:'',state:'failed'};
  try{
   let retainedTmp:string|undefined;
   if(this.profile.retainGnuLtoInputs){
    const base=this.profile.buildDirRelative+'/.studio-link-inputs';
    await mkdir(this.root+'/'+base,{mode:0o700}).catch((e:NodeJS.ErrnoException)=>{if(e.code!=='EEXIST')throw e;});
    if(!(await stat(await checkedPath(this.root,base))).isDirectory())throw new Error('Link input retention destination is not a directory.');
    const relative=base+'/'+id;
    await mkdir(this.root+'/'+relative,{mode:0o700});
    retainedTmp=await checkedPath(this.root,relative);
   }
   const before:InputFingerprint[]=await inputs(this.profile);const preBinary:FileFingerprint|undefined=await inspect(this.root,this.profile.outputBinaryRelative,true).catch(()=>undefined);const git=await gitIdentity(this.root);
   const compilerBefore=this.profile.compilerDependencyMode==='ninja'?await fingerprintNinjaDependencies(this.root+'/'+this.profile.buildDirRelative).catch(()=>undefined):undefined;
   const toolchainBefore=this.profile.compilerDependencyMode==='ninja'?(await readBuildToolchainEvidence(this.root+'/'+this.profile.buildDirRelative).catch(()=>undefined))?.compilers:undefined;
   await new Promise<void>((resolve,reject)=>{
    const child=(this.hooks.spawn??spawn)(this.hooks.cmake??CMAKE,['--build',this.root+'/'+this.profile.buildDirRelative,'--target',this.profile.target,'--parallel',String(this.profile.parallelism)],{cwd:this.root,shell:false,env:{PATH:'/usr/local/cuda-12.8/bin:/usr/local/bin:/usr/bin:/bin',HOME:process.env.HOME??'/home/arch',LANG:'C.UTF-8',...(retainedTmp?{TMPDIR:retainedTmp}:{})},stdio:['ignore','pipe','pipe']});this.child=child;
    for(const [stream,kind] of [[child.stdout,'stdout'],[child.stderr,'stderr']] as const){const decoder=new StringDecoder('utf8');stream?.on('data',(chunk:Buffer)=>this.log?.append(kind,decoder.write(chunk)));stream?.on('end',()=>{const tail=decoder.end();if(tail)this.log?.append(kind,tail);});}
    child.once('error',()=>reject(new Error('Build process could not start.')));child.once('close',(code,signal)=>{result.exitCode=code;result.signal=signal;if(code===0)resolve();else reject(new Error('Build exited unsuccessfully.'));});
   });const manifest=await makeManifest(this.profile,this.projectId,id,startedAt,before,preBinary,git,compilerBefore,toolchainBefore);await saveManifest(this.profile,manifest);this.current.lastSuccessfulBuild=manifest;result.state='succeeded';
  }catch(e){result.error=e instanceof Error?e.message:'Build failed';}
  finally{result.finishedAt=new Date().toISOString();if(result.error)this.log?.append('stderr',result.error);this.log?.append('state',undefined,result.state);this.current.latestResult=result;this.current.state=result.state;this.child=undefined;await this.refreshFreshness(true);delete this.current.activeBuildId;}
 }
 events(buildId:string){if(!this.log||this.log.buildId!==buildId)throw new BuildError('Unknown or expired build ID.',404);return this.log.snapshot();}
 isActive(){return !!this.current.activeBuildId;}
 // CLI refuses to abandon a live compiler; no generic cancellation/PID API.
 get processId(){return this.child?.pid;}
}
