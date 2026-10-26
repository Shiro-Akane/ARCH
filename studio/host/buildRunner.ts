/**
 * Own one build operation and its evidence lifecycle.
 * Workflow: validate the selected profile, snapshot dependencies, launch the
 * controlled toolchain, collect bounded events, and publish freshness only after
 * success and stable input checks. Closing a client does not abandon a compiler.
 */
import {readToolRuntimeEvidence,toolRuntimeRoots,changedRuntimeInputs,runtimeFingerprint,runtimeEvidenceFiles,validRuntimeEvidence} from './toolRuntimeEvidence.ts';
import {mkdir,stat} from 'node:fs/promises';
import {checkedPath} from './files.ts';
import {readBuildToolchainEvidence,readBuildConfigurationInputs,sameConfigurationInputs,HOST_BUILD_PATH} from './cmakeEvidence.ts';
import {fingerprintLinkDependencies,changedLinkInputs} from './linkDependencies.ts';
import {fingerprintNinjaDependencies,sameCompilerInputs} from './ninjaDependencies.ts';
import {inputs,inspect,gitIdentity,makeManifest,saveManifest,loadManifest,same} from './buildManifest.ts';
import {BuildLog} from './buildLog.ts';
import {StringDecoder} from 'node:string_decoder';
import {spawn} from 'node:child_process';
import {spawnOwned} from './hostProcess.ts';
import type {WindowsHostTools} from './hostProcess.ts';
import type {ChildProcess,SpawnOptions} from 'node:child_process';
import {randomUUID} from 'node:crypto';
import {CMAKE,validateProfile,profileFingerprint} from './buildProfile.ts';
import type {BuildProfile,BuildSnapshot,BuildResult,InputFingerprint,FileFingerprint,BuildManifest,ObservedBuildInputs} from '../src/host/contracts.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
export class BuildError extends Error { status:number; constructor(message:string,status=400){super(message);this.status=status;} }
function sameRecordedInputs(now:InputFingerprint[],recorded:InputFingerprint[]){
 return now.length===recorded.length&&now.every((item,index)=>
  item.relativePath===recorded[index].relativePath&&same(item.fingerprint,recorded[index].fingerprint));
}
// Module-only seam for deterministic negative tests; never exposed to HTTP.
export interface RunnerHooks { cmake?:string; spawn?:typeof spawn; windowsTools?:WindowsHostTools }
export class BuildRunner {
 readonly profile:BuildProfile; readonly root:string; readonly projectId:string;
 private freshnessQuery:Promise<BuildSnapshot>|null=null;
 private hooks:RunnerHooks; private current:BuildSnapshot; private child?:ChildProcess; private log?:BuildLog;
 constructor(root:string,projectId:string,profile:BuildProfile,hooks:RunnerHooks={}){
  this.root=root;this.projectId=projectId;this.profile=structuredClone(profile);this.hooks={...hooks,...(hooks.windowsTools?{windowsTools:structuredClone(hooks.windowsTools)}:{})};
  this.current={protocolVersion:PROTOCOL_VERSION,projectId,profile:this.profile,configured:false,state:'not-configured',mappingState:profile.caseId?'configured':'unknown',binaryState:'freshness-unknown',freshnessReason:'No successful Studio build manifest.',changedInputs:[]};
 }
 async initialize(){await this.validate();this.current.lastSuccessfulBuild=await loadManifest(this.profile);return this.refreshFreshness();}
 async refreshFreshness(finalizing=false){
  if(this.freshnessQuery){
   if(!finalizing)return this.freshnessQuery;
   await this.freshnessQuery;
  }
  const query=this.measureFreshness(finalizing);
  this.freshnessQuery=query;
  try{return await query;}finally{if(this.freshnessQuery===query)this.freshnessQuery=null;}
 }
 private async measureFreshness(finalizing=false){
  if(this.isActive()&&!finalizing)return this.snapshot();
  const m=this.current.lastSuccessfulBuild;
  const evidence:Pick<BuildSnapshot,'changedInputs'|'binaryState'|'freshnessReason'|'observedInputs'>={
   changedInputs:[],binaryState:this.current.binaryState,freshnessReason:this.current.freshnessReason,
   ...(this.profile.observedInputScope?{observedInputs:this.observed('unknown','Matching recorded Build evidence has not been established.')}:{})};
  try{const binary=await inspect(this.root,this.profile.outputBinaryRelative,true);
   if(!m){evidence.binaryState='freshness-unknown';evidence.freshnessReason='Executable available, but no successful Studio build provenance.';}
   else if(m.buildProfileFingerprint!==profileFingerprint(this.profile)){evidence.binaryState='freshness-unknown';evidence.freshnessReason='Build configuration changed since last successful Build.';}
   else{
    for(const old of m.trackedInputFingerprints){try{if(!same(old.fingerprint,await inspect(this.root,old.relativePath)))evidence.changedInputs.push(old.relativePath);}catch{evidence.changedInputs.push(old.relativePath);}}
    let configurationUnknown=false;
    if(this.profile.compilerDependencyMode==='ninja'){
     if(!m.configurationInputs||!m.configurationInputs.generatorTools||m.configurationInputError||m.configurationInputsStableDuringBuild!==true)configurationUnknown=true;
     else try{
      const now=await readBuildConfigurationInputs(this.root,this.root+'/'+this.profile.buildDirRelative,this.hooks.windowsTools);
      if(!sameConfigurationInputs(m.configurationInputs,now)){
       const tools=new Map(m.configurationInputs.generatorTools.map(t=>[t.role,t]));
       for(const t of now.generatorTools??[]){const old=tools.get(t.role);if(!old||old.path!==t.path||old.resolvedPath!==t.resolvedPath||old.sha256!==t.sha256||old.size!==t.size)evidence.changedInputs.push(t.path);}
       const prior=new Map(m.configurationInputs.inputs.map(f=>[f.path,f]));
       for(const f of now.inputs){const old=prior.get(f.path);if(!old||old.sha256!==f.sha256||old.size!==f.size)evidence.changedInputs.push(f.path);prior.delete(f.path);}
       evidence.changedInputs.push(...prior.keys());
       if(!evidence.changedInputs.length)evidence.changedInputs.push('CMake configuration input graph');
      }
     }catch{configurationUnknown=true;}
    }
    let compilerUnknown=false;
    if(this.profile.compilerDependencyMode==='ninja'){
     if(this.profile.observedInputScope&&(!m.compilerInputs||m.compilerInputError||m.compilerInputsStableDuringBuild!==true))compilerUnknown=true;
     if(m.compilerInputs)try{
      const now=await fingerprintNinjaDependencies(this.root+'/'+this.profile.buildDirRelative,this.hooks.windowsTools);
      if(!sameCompilerInputs(m.compilerInputs,now)){
       const previous=new Map(m.compilerInputs.files.map(f=>[f.path,f]));
       for(const file of now.files){const old=previous.get(file.path);if(!old||old.sha256!==file.sha256||old.size!==file.size)evidence.changedInputs.push(file.path);previous.delete(file.path);}
       evidence.changedInputs.push(...previous.keys());
       if(!evidence.changedInputs.length)evidence.changedInputs.push('compiler dependency graph');
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
      const now=(await readBuildToolchainEvidence(this.root+'/'+this.profile.buildDirRelative,this.hooks.windowsTools)).compilers;
      const previous=new Map(m.compilerDrivers.map(c=>[c.language,c]));
      for(const driver of now){
       const old=previous.get(driver.language);
       if(!old||old.path!==driver.path||old.resolvedPath!==driver.resolvedPath||old.sha256!==driver.sha256||
          old.size!==driver.size||old.id!==driver.id||old.version!==driver.version)
        evidence.changedInputs.push(driver.path);
       if(driver.id==='GNU'){
        if(!old?.components?.length||!old.specsSha256||!driver.components?.length)toolchainUnknown=true;
        else{
         if(old.specsSha256!==driver.specsSha256)evidence.changedInputs.push(driver.path+' specs');
         const prior=new Map(old.components.map(c=>[c.role,c]));
         for(const component of driver.components){
          const saved=prior.get(component.role);
          if(component.role.startsWith('selected-')&&!saved){toolchainUnknown=true;continue;}
          if(!saved||saved.path!==component.path||saved.resolvedPath!==component.resolvedPath||saved.sha256!==component.sha256||saved.size!==component.size)evidence.changedInputs.push(component.path);
          prior.delete(component.role);
         }
         for(const removed of prior.values())evidence.changedInputs.push(removed.path);
        }
       }
       previous.delete(driver.language);
      }
      for(const old of previous.values())evidence.changedInputs.push(old.path);
     }catch{toolchainUnknown=true;}
    }
    let runtimeUnknown=false,observedRuntimeUnknown=true;
    if(this.profile.compilerDependencyMode==='ninja'){
     if(!m.toolRuntime||m.toolRuntimeError||m.toolRuntimeStableDuringBuild!==true)runtimeUnknown=true;
     else try{
      for(const old of runtimeEvidenceFiles(m.toolRuntime)){try{const f=await runtimeFingerprint(old.path);if(f.resolvedPath!==old.resolvedPath||f.sha256!==old.sha256||f.size!==old.size)evidence.changedInputs.push(old.path);}catch{evidence.changedInputs.push(old.path);}}
      const configuration=await readBuildConfigurationInputs(this.root,this.root+'/'+this.profile.buildDirRelative,this.hooks.windowsTools);
      const drivers=(await readBuildToolchainEvidence(this.root+'/'+this.profile.buildDirRelative,this.hooks.windowsTools)).compilers;
      const now=await readToolRuntimeEvidence(toolRuntimeRoots(configuration,drivers,this.hooks.windowsTools),undefined,this.hooks.windowsTools);
      evidence.changedInputs.push(...changedRuntimeInputs(m.toolRuntime,now));
      runtimeUnknown=now.unresolved.length>0||this.profile.dependenciesComplete;
      // Coverage gaps do not exempt any recorded physical candidate from hashes.
      observedRuntimeUnknown=!validRuntimeEvidence(now)||now.version!==2||
       now.unresolved.some(edge=>edge.reason!=='system-boundary'&&edge.reason!=='api-set-boundary')||
       now.nodes.some(node=>node.delayImportState!=='observed');
     }catch{runtimeUnknown=true;}
    }
    let linkUnknown=false,missingLinkInputs=0;
    if(this.profile.linkDependencyFile){
     if(!m.linkInputs||m.linkInputError)linkUnknown=true;
     else try{
      const now=await fingerprintLinkDependencies(this.root+'/'+this.profile.buildDirRelative,this.profile.linkDependencyFile,this.root+'/'+this.profile.outputBinaryRelative);
      evidence.changedInputs.push(...changedLinkInputs(m.linkInputs,now));
      missingLinkInputs=now.unavailable.length;linkUnknown=missingLinkInputs>0;
     }catch{linkUnknown=true;}
    }
    if(evidence.changedInputs.length||!m.inputsStableDuringBuild||m.compilerDriversStableDuringBuild===false){evidence.binaryState='needs-build';evidence.freshnessReason=evidence.changedInputs.length?'Tracked build inputs changed since successful Build.':'Tracked inputs or compiler toolchain changed during Build; build again for a stable snapshot.';}
    else if(compilerUnknown){evidence.binaryState='freshness-unknown';evidence.freshnessReason='Compiler dependency evidence unavailable or stale.';}
    else if(toolchainUnknown){evidence.binaryState='freshness-unknown';evidence.freshnessReason='Compiler toolchain identity is incomplete or unavailable.';}
    else if(configurationUnknown){evidence.binaryState='freshness-unknown';evidence.freshnessReason='CMake configuration input evidence unavailable or unstable.';}
    else if(runtimeUnknown){evidence.binaryState='freshness-unknown';evidence.freshnessReason='Static tool runtime identity is missing, unstable, or actual loader selection remains unverified.';}
    else if(linkUnknown){evidence.binaryState='freshness-unknown';evidence.freshnessReason=missingLinkInputs?('Linker input evidence is incomplete: '+missingLinkInputs+' recorded inputs are missing. Full dependency freshness is unknown.'):'Linker input evidence is incomplete or unavailable.';}
    else if(m.compilerInputsStableDuringBuild===false){evidence.binaryState='freshness-unknown';evidence.freshnessReason='Compiler input stability was not established across Build.';}
    else if(!same(binary,m.outputBinary.fingerprint)){evidence.binaryState='freshness-unknown';evidence.freshnessReason='Executable differs from last successful Build manifest.';}
    else{evidence.binaryState=this.profile.dependenciesComplete?'built-from-current-tracked-inputs':'freshness-unknown';evidence.freshnessReason=this.profile.dependenciesComplete?'Explicit tracked inputs match the successful Build.':'Tracked inputs match; full dependency coverage is unknown.';}
    if(this.profile.observedInputScope){
     const recordedError=[m.compilerInputError,m.compilerDriverError,m.configurationInputError,
      m.toolRuntimeError,m.linkInputError].some(error=>error!==undefined);
     if(evidence.changedInputs.length||evidence.binaryState==='needs-build')
      evidence.observedInputs=this.observed('changed','Recorded Build inputs changed; Build is required.');
     else if(!this.current.configured||recordedError||compilerUnknown||toolchainUnknown||configurationUnknown||linkUnknown||observedRuntimeUnknown||
       m.inputsStableDuringBuild!==true||m.compilerInputsStableDuringBuild!==true||
       m.compilerDriversStableDuringBuild!==true||m.configurationInputsStableDuringBuild!==true||
       m.toolRuntimeStableDuringBuild!==true||!m.compilerDrivers?.every(driver=>
        driver.id==='GNU'||(driver.language==='RC'&&driver.id==='GNU windres')))
      evidence.observedInputs=this.observed('unknown','Required recorded dependency evidence is missing, unstable or outside the reviewed scope.');
     else try{
      const currentInputs=await inputs(this.profile);
      const currentBinary=await inspect(this.root,this.profile.outputBinaryRelative,true);
      if(!sameRecordedInputs(currentInputs,m.trackedInputFingerprints)||!same(binary,m.outputBinary.fingerprint)||!same(currentBinary,m.outputBinary.fingerprint))
       evidence.observedInputs=this.observed('changed','Tracked input or binary identity changed during the evidence scan.');
      else {
       evidence.observedInputs=this.observed('current','Recorded Build inputs match; system/API-set resolution, dynamic loading and unrecorded intermediates remain outside this scope.');
       evidence.freshnessReason='Recorded Build inputs match within observed-build-inputs-v1. Full dependency freshness remains unknown.';
      }
     }catch{
      evidence.observedInputs=this.observed('unknown','Recorded file identities could not be rechecked.');
     }
    }
   }
  }catch{evidence.binaryState='missing';evidence.freshnessReason='Expected executable missing or unreadable; no readiness claim.';}
  // Publish only a completed scan of the still-current manifest. Polling must
  // never observe a cleared or partially rebuilt changed-input list.
  if(this.current.lastSuccessfulBuild===m&&(!this.isActive()||finalizing))Object.assign(this.current,evidence);
  return this.snapshot();
 }
 private observed(state:ObservedBuildInputs['state'],reason:string):ObservedBuildInputs{
  return {scope:'observed-build-inputs-v1',state,reason};
 }
 snapshot(){
  const result=structuredClone(this.current);
  if(result.observedInputs&&(result.activeBuildId||!result.configured))
   result.observedInputs=this.observed('unknown','Build is unavailable or active.');
  return result;
 }
 /** Admit successful recorded Build evidence; consumers own their capabilities. */
 async requireBuildEvidence(profileId:string):Promise<BuildManifest>{
  const configured=await this.validate();
  const refreshed=await this.refreshFreshness();
  const manifest=this.current.lastSuccessfulBuild;
  if(!configured.configured||!this.current.configured||this.isActive())throw new BuildError('Build unavailable or active.',409);
  if(this.profile.id!==profileId)throw new BuildError('Preview profile does not match the configured Build.',409);
  if(refreshed.binaryState==='needs-build')throw new BuildError('Tracked source or compiler inputs changed. Build required before real preview.',409);
  if(!manifest||refreshed.lastSuccessfulBuild?.buildId!==manifest.buildId||!manifest.inputsStableDuringBuild||
   manifest.buildProfileFingerprint!==profileFingerprint(this.profile))
   throw new BuildError('Build required before real preview: no matching successful manifest.',409);
  if(this.profile.observedInputScope&&refreshed.observedInputs?.state!=='current')
   throw new BuildError(refreshed.observedInputs?.reason??'Recorded Build input evidence is unavailable.',409);
  if(!sameRecordedInputs(await inputs(this.profile),manifest.trackedInputFingerprints))
   throw new BuildError('Tracked source changed. Build required before real preview.',409);
  if(!same(await inspect(this.root,this.profile.outputBinaryRelative,true),manifest.outputBinary.fingerprint))
   throw new BuildError('Binary differs from successful Build. Build required before real preview.',409);
  if(this.isActive()||this.current.lastSuccessfulBuild!==manifest||manifest.buildProfileFingerprint!==profileFingerprint(this.profile)||
   (this.profile.observedInputScope&&this.current.observedInputs?.state!=='current'))
   throw new BuildError('Build identity changed during readiness verification.',409);
  return structuredClone(manifest);
 }
 async validate(){try{await validateProfile(this.root,this.profile,this.hooks.windowsTools?.cmake??this.hooks.cmake??CMAKE,this.hooks.windowsTools);this.current.configured=true;this.current.reason=undefined;if(this.current.state==='not-configured')this.current.state='ready';}catch(e){this.current.configured=false;this.current.reason=e instanceof Error?e.message:'Build profile unavailable';if(!this.current.activeBuildId)this.current.state='not-configured';}return this.snapshot();}
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
   const configurationBefore=this.profile.compilerDependencyMode==='ninja'?await readBuildConfigurationInputs(this.root,this.root+'/'+this.profile.buildDirRelative,this.hooks.windowsTools).catch(()=>undefined):undefined;
   const compilerBefore=this.profile.compilerDependencyMode==='ninja'?await fingerprintNinjaDependencies(this.root+'/'+this.profile.buildDirRelative,this.hooks.windowsTools).catch(()=>undefined):undefined;
   const toolchainBefore=this.profile.compilerDependencyMode==='ninja'?(await readBuildToolchainEvidence(this.root+'/'+this.profile.buildDirRelative,this.hooks.windowsTools).catch(()=>undefined))?.compilers:undefined;
   const runtimeBefore=this.profile.compilerDependencyMode==='ninja'?await Promise.resolve().then(()=>readToolRuntimeEvidence(toolRuntimeRoots(configurationBefore,toolchainBefore,this.hooks.windowsTools),undefined,this.hooks.windowsTools)).catch(()=>undefined):undefined;
   await new Promise<void>((resolve,reject)=>{
    const program=this.hooks.windowsTools?.cmake??this.hooks.cmake??CMAKE;
    const args=['--build',this.root+'/'+this.profile.buildDirRelative,'--target',this.profile.target,'--parallel',String(this.profile.parallelism)];
    const options:SpawnOptions={cwd:this.root,shell:false,
     env:{PATH:HOST_BUILD_PATH,HOME:process.env.HOME??'/home/arch',LANG:'C.UTF-8',...(retainedTmp?{TMPDIR:retainedTmp}:{})},
     stdio:['ignore','pipe','pipe']};
    const child=this.hooks.spawn?this.hooks.spawn(program,args,options):
     spawnOwned(program,args,options,this.hooks.windowsTools,retainedTmp);
    this.child=child;
    for(const [stream,kind] of [[child.stdout,'stdout'],[child.stderr,'stderr']] as const){const decoder=new StringDecoder('utf8');stream?.on('data',(chunk:Buffer)=>this.log?.append(kind,decoder.write(chunk)));stream?.on('end',()=>{const tail=decoder.end();if(tail)this.log?.append(kind,tail);});}
    child.once('error',()=>reject(new Error('Build process could not start.')));child.once('close',(code,signal)=>{result.exitCode=code;result.signal=signal;if(code===0)resolve();else reject(new Error('Build exited unsuccessfully.'));});
   });const manifest=await makeManifest(this.profile,this.projectId,id,startedAt,before,preBinary,git,compilerBefore,toolchainBefore,configurationBefore,runtimeBefore,this.hooks.windowsTools);await saveManifest(this.profile,manifest,this.hooks.windowsTools);this.current.lastSuccessfulBuild=manifest;result.state='succeeded';
  }catch(e){result.error=e instanceof Error?e.message:'Build failed';}
  finally{
   // The compiler has ended, but the build remains busy until the final
   // provenance/freshness scan completes. No terminal result or log may claim
   // success while Run is still barred by this attempt's active identity.
   this.child=undefined;
   try{await this.refreshFreshness(true);}
   catch(e){
    const detail=e instanceof Error?e.message:'Unknown freshness error';
    const failure='Final build freshness verification failed: '+detail;
    result.state='failed';result.error=result.error?result.error+' '+failure:failure;
    this.current.binaryState='freshness-unknown';this.current.freshnessReason=failure;
   }
   // Publish this one completed terminal snapshot synchronously, after all
   // fallible final evidence work. Retain captured manifests and precise process
   // exit/signal provenance; a failed scan never grants executable readiness.
   result.finishedAt=new Date().toISOString();
   delete this.current.activeBuildId;
   this.current.latestResult=result;this.current.state=result.state;
   if(result.error)this.log?.append('stderr',result.error);
   this.log?.append('state',undefined,result.state);
  }
 }
 events(buildId:string){if(!this.log||this.log.buildId!==buildId)throw new BuildError('Unknown or expired build ID.',404);return this.log.snapshot();}
 isActive(){return !!this.current.activeBuildId;}
 // CLI refuses to abandon a live compiler; no generic cancellation/PID API.
 get processId(){return this.child?.pid;}
}
