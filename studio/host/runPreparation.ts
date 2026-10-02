import {execFile} from 'node:child_process';
import {randomUUID} from 'node:crypto';
import {checkedPath,fingerprint} from './files.ts';
import {sameFingerprint} from './config.ts';
import {BuildError} from './buildRunner.ts';
import {pathPreflight} from './pathPreflight.ts';
import {validateRegistry} from '../src/host/workflowValidation.ts';
import {validateConfigurationSchema,validateConfigurationInspection} from '../src/host/configurationValidation.ts';
import type {ConfigReadResponse,FileFingerprint} from '../src/host/contracts.ts';
import type {ConfigurationInspection,PathCheck} from '../src/host/configurationContracts.ts';
export interface PrepareRunRequest {projectId:string;caseId:string;configRevision:string;mode:'run'|'restart'}
export interface RunPreparation {
 planId:string;projectId:string;caseId:string;mode:'run'|'restart';createdAt:string;
 binary:{relativePath:string;fingerprint:FileFingerprint;sourceClaim:'compiled-version-only'};
 config:{relativePath:string;fingerprint:FileFingerprint};
 inspection:ConfigurationInspection;pathChecks:PathCheck[];issues:string[];
 canConfirm:boolean;simulationReadiness:'core-startup-pending';
 checkpointPath:string|null;
 pendingChecks:string[];
}
/** Static run preflight on the selected binary, independent of Preview capability/freshness. */
export class RunPreparationRunner {
 private active=false;
 readonly root:string;readonly projectId:string;readonly binaryRelativePath:string;
 private config:()=>Promise<ConfigReadResponse>;
 constructor(root:string,projectId:string,binaryRelativePath:string,config:()=>Promise<ConfigReadResponse>){
  this.root=root;this.projectId=projectId;this.binaryRelativePath=binaryRelativePath;this.config=config;
 }
 isActive(){return this.active;}
 private async binary():Promise<FileFingerprint>{
  const f=await fingerprint(this.root,this.binaryRelativePath,'executable');
  if(!f.exists||f.error||!f.sha256||f.size===undefined||!f.modifiedTime)throw new BuildError('Selected executable unavailable.',409);
  return {sha256:f.sha256,size:f.size,modifiedTime:f.modifiedTime};
 }
 private async query(args:string[],text=''):Promise<unknown>{
  const executable=await checkedPath(this.root,this.binaryRelativePath);
  return new Promise((resolve,reject)=>{
   const child=execFile(executable,args,{cwd:this.root,shell:false,encoding:'utf8',timeout:10000,maxBuffer:8*1024*1024,
    env:{PATH:'/usr/bin:/bin',LANG:'C.UTF-8',OMP_NUM_THREADS:'1',CUDA_VISIBLE_DEVICES:''}},(error,stdout)=>{
     if(error&&error.code!==3&&error.code!==7){reject(new BuildError('Core run preflight failed or exceeded its bounds.',502));return;}
     try{resolve(JSON.parse(stdout));}catch{reject(new BuildError('Malformed Core run preflight response.',502));}
    });
   child.stdin?.on('error',()=>undefined);child.stdin?.end(text);
  });
 }
 async prepare(request:PrepareRunRequest):Promise<RunPreparation>{
  const keys=['projectId','caseId','configRevision','mode'];
  if(!request||Object.keys(request).length!==keys.length||Object.keys(request).some(k=>!keys.includes(k))||
   request.projectId!==this.projectId||typeof request.caseId!=='string'||!request.caseId||request.caseId.length>128||
   !['run','restart'].includes(request.mode)||typeof request.configRevision!=='string'||!/^[a-f0-9]{64}$/.test(request.configRevision))
   throw new BuildError('Invalid run preparation request.');
  if(this.active)throw new BuildError('Run preparation already active.',409);
  this.active=true;
  try{
   const saved=await this.config();
   if(saved.projectId!==this.projectId||saved.fingerprint.sha256!==request.configRevision)
    throw new BuildError('Save the exact Working Copy before preparing Run/Restart.',409);
   const binary=await this.binary();
   const registry=validateRegistry(await this.query(['--list-cases']));
   if(!registry.some(c=>c.caseId===request.caseId))throw new BuildError('Case is not registered in the selected binary.');
   const schema=validateConfigurationSchema(await this.query(['--config-schema']));
   const requestId=randomUUID();
   const inspection=validateConfigurationInspection(await this.query(['--inspect-config',request.caseId,'--config-stdin','--request-id',requestId],saved.text),
    {caseId:request.caseId,configRevision:request.configRevision,requestId});
   const allPaths=await pathPreflight(schema,inspection,this.root);
   const pathChecks=allPaths.filter(check=>inspection.parameters.find(p=>p.key===check.key)?.applicability.state!=='not-applicable');
   const issues:string[]=[];
   if(inspection.status!=='ok'||inspection.completeness.state!=='complete'||inspection.diagnostics.some(d=>d.severity==='error')||Object.values(inspection.coverage).some(v=>!v))
    issues.push('Core declared configuration checks are incomplete or invalid.');
   for(const check of pathChecks){
    const parameter=inspection.parameters.find(p=>p.key===check.key);
    if(check.status==='error'||check.status==='unable-to-check'||(check.status==='not-set'&&parameter?.requirement.required===true))
     issues.push(check.key+': '+check.message);
   }
   const value=(key:string)=>inspection.parameters.find(p=>p.key===key)?.resolvedValue;
   const restart=value('restart');
   if(typeof restart!=='boolean')issues.push('Core did not resolve the restart mode.');
   else if(restart!==(request.mode==='restart'))issues.push(request.mode==='run'?'Saved configuration requests Restart; select Restart explicitly.':'Restart requires restart=true in the explicitly saved configuration.');
   const checkpointPath=request.mode==='restart'?pathChecks.find(p=>p.key==='restart_file')?.resolvedPath??null:null;
   if(request.mode==='restart'&&!checkpointPath)issues.push('Restart checkpoint path is unavailable.');
   const latest=await this.config();
   if(latest.relativePath!==saved.relativePath||!sameFingerprint(latest.fingerprint,saved.fingerprint)||!sameFingerprint(await this.binary(),binary))
    throw new BuildError('Configuration or executable changed during run preparation.',409);
   return {planId:randomUUID(),projectId:this.projectId,caseId:request.caseId,mode:request.mode,createdAt:new Date().toISOString(),
    binary:{relativePath:this.binaryRelativePath,fingerprint:binary,sourceClaim:'compiled-version-only'},
    config:{relativePath:saved.relativePath,fingerprint:saved.fingerprint},inspection,pathChecks,issues,
    canConfirm:issues.length===0,simulationReadiness:'core-startup-pending',checkpointPath,
    pendingChecks:['Authoritative Core Setup and runtime configuration','EOS and network resource loading','Execution backend availability',
     ...(request.mode==='restart'?['Core checkpoint identity/layout and continuation compatibility']:[])]};
  }finally{this.active=false;}
 }
}
