import {pathPreflight} from './pathPreflight.ts';
import {validateConfigurationSchema,validateConfigurationInspection} from '../src/host/configurationValidation.ts';
import {execFile} from 'node:child_process';
import {createHash,randomUUID} from 'node:crypto';
import {fingerprint,checkedPath} from './files.ts';
import {BuildError} from './buildRunner.ts';
import type {PreviewRunner} from './previewRunner.ts';
import type {ConfigurationRequest,ConfigurationSchema} from '../src/host/configurationContracts.ts';
import {validateRegistry} from '../src/host/workflowValidation.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
export interface SelectedConfigurationBinary {root:string;projectId:string;binaryRelativePath:string}
/** Only the Host chooses executable, arguments, cwd and environment. */
export class ConfigurationAdapter {
 assertCase?:(caseId:string)=>Promise<void>;
 private active=false;
 private registryQuery:Promise<Record<string,unknown>>|null=null;
 private schemaCache?:{buildId:string;sha:string;core:ConfigurationSchema};
 private preview?:PreviewRunner;
 private selected?:SelectedConfigurationBinary;
 private get target(){return this.selected??{root:this.preview!.build.root,projectId:this.preview!.build.projectId,binaryRelativePath:this.preview!.build.profile.outputBinaryRelative};}
 private discover?:()=>Promise<{cases:{caseId:string}[]}>;
 constructor(source:PreviewRunner|SelectedConfigurationBinary,discover?:()=>Promise<{cases:{caseId:string}[]}>){if('binaryRelativePath' in source)this.selected=source;else this.preview=source;this.discover=discover;}
 private async ready(){
  if(this.selected){
   const f=await fingerprint(this.selected.root,this.selected.binaryRelativePath,'executable');
   if(!f.exists||f.error||!f.sha256)throw new BuildError('Selected configuration binary unavailable.',409);
   // Opaque binary identity, explicitly not a successful/current Build claim.
   return {buildId:'selected-binary:'+f.sha256,outputBinary:{fingerprint:{sha256:f.sha256}}};
  }
  const s=await this.preview!.readiness();
  if(!s.ready||!s.build)throw new BuildError('Configuration inspection requires a current successful Preview build.',409);
  return s.build;
 }
 private async run(args:string[],text?:string):Promise<Record<string,unknown>> {
  // Static registration can overlap schema/inspection at startup. Coalesce it
  // separately: at most one registry child plus one configuration child.
  if(args.length===1&&args[0]==='--list-cases'){
   if(this.registryQuery)return this.registryQuery;
   const query=this.runCommand(args,text,true);
   this.registryQuery=query;
   try{return await query;}finally{if(this.registryQuery===query)this.registryQuery=null;}
  }
  return this.runCommand(args,text);
 }
 private async runCommand(args:string[],text?:string,registry=false):Promise<Record<string,unknown>> {
  if(!registry&&this.active)throw new BuildError('Configuration inspection already active.',409);
  if(!registry)this.active=true;
  try {
   const build=this.target;
   const binary=await checkedPath(build.root,build.binaryRelativePath);
   return await new Promise((resolve,reject)=>{
    const child=execFile(binary,args,{cwd:build.root,shell:false,encoding:'utf8',timeout:10000,maxBuffer:8*1024*1024,env:{PATH:'/usr/bin:/bin',LANG:'C.UTF-8',OMP_NUM_THREADS:'1',CUDA_VISIBLE_DEVICES:''}},(error,stdout)=>{
     // Core uses exit 3 for invalid/incomplete input and 7 for bounded evidence errors.
     if(error&&error.code!==3&&error.code!==7){reject(new BuildError('Configuration command failed or exceeded its limits.',502));return;}
     try{const value:unknown=JSON.parse(stdout);if(!value||typeof value!=='object'||Array.isArray(value))throw new Error();resolve(value as Record<string,unknown>);}catch{reject(new BuildError('Invalid Core configuration response.',502));}
    });
    child.stdin?.on('error',()=>undefined);child.stdin?.end(text??'');
   });
  }finally{if(!registry)this.active=false;}
 }
 async discovery(){
  const before=await this.ready();
  const cases=validateRegistry(await this.run(['--list-cases']));
  const after=await this.ready();
  if(before.buildId!==after.buildId||before.outputBinary.fingerprint.sha256!==after.outputBinary.fingerprint.sha256)throw new BuildError('Binary changed during registry request.',409);
  // Registration is available independently. No Host field/AMR execution profile
  // has been negotiated on this static-only path.
  return {protocolVersion:PROTOCOL_VERSION,projectId:this.target.projectId,buildId:before.buildId,binarySha256:before.outputBinary.fingerprint.sha256,cases,fieldModels:[],amr:null};
 }
 async schema(){
  const before=await this.ready();const cache=this.schemaCache;const core=cache?.buildId===before.buildId&&cache.sha===before.outputBinary.fingerprint.sha256?cache.core:await this.run(['--config-schema']);const after=await this.ready();
  if(before.buildId!==after.buildId||before.outputBinary.fingerprint.sha256!==after.outputBinary.fingerprint.sha256)throw new BuildError('Build changed during schema request.',409);
  this.schemaCache={buildId:before.buildId,sha:before.outputBinary.fingerprint.sha256,core:validateConfigurationSchema(core)};
  return {protocolVersion:PROTOCOL_VERSION,projectId:this.target.projectId,buildId:before.buildId,binarySha256:before.outputBinary.fingerprint.sha256,core};
 }
 async inspect(r:ConfigurationRequest){
  const keys=['projectId','caseId','configText','configRevision'];
  if(Object.keys(r).length!==keys.length||Object.keys(r).some(k=>!keys.includes(k))||r.projectId!==this.target.projectId||typeof r.caseId!=='string'||typeof r.configText!=='string'||r.configText.includes('\0')||Buffer.byteLength(r.configText)>1024*1024||Buffer.from(r.configText).toString('utf8')!==r.configText||r.configRevision!==createHash('sha256').update(r.configText).digest('hex'))throw new BuildError('Invalid configuration inspection request.');
  await this.assertCase?.(r.caseId);
  if(this.discover ? !(await this.discover()).cases.some(c=>c.caseId===r.caseId) : this.selected ? !validateRegistry(await this.run(['--list-cases'])).some(c=>c.caseId===r.caseId) : r.caseId!==this.preview!.profile.caseId)throw new BuildError('Invalid configuration inspection request: case is not registered in selected binary.');
  const schema=await this.schema();
  const before=await this.ready();const requestId=randomUUID();
  if(schema.buildId!==before.buildId||schema.binarySha256!==before.outputBinary.fingerprint.sha256)throw new BuildError('Build changed after schema request.',409);
  const core=validateConfigurationInspection(await this.run(['--inspect-config',r.caseId,'--config-stdin','--request-id',requestId],r.configText),{caseId:r.caseId,configRevision:r.configRevision,requestId});
  const pathChecks=await pathPreflight(validateConfigurationSchema(schema.core),core,this.target.root);
  const after=await this.ready();
  if(before.buildId!==after.buildId||before.outputBinary.fingerprint.sha256!==after.outputBinary.fingerprint.sha256)throw new BuildError('Build changed during inspection.',409);
  return {protocolVersion:PROTOCOL_VERSION,identity:{projectId:r.projectId,caseId:r.caseId,configRevision:r.configRevision,requestId,buildId:before.buildId,binarySha256:before.outputBinary.fingerprint.sha256},core,pathChecks};
 }
}
