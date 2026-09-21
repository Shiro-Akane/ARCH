import {pathPreflight} from './pathPreflight.ts';
import {validateConfigurationSchema,validateConfigurationInspection} from '../src/host/configurationValidation.ts';
import {execFile} from 'node:child_process';
import {createHash,randomUUID} from 'node:crypto';
import {checkedPath} from './files.ts';
import {BuildError} from './buildRunner.ts';
import type {PreviewRunner} from './previewRunner.ts';
import type {ConfigurationRequest,ConfigurationSchema} from '../src/host/configurationContracts.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
/** Only the Host chooses executable, arguments, cwd and environment. */
export class ConfigurationAdapter {
 private active=false;
 private schemaCache?:{buildId:string;sha:string;core:ConfigurationSchema};
 private preview:PreviewRunner;
 constructor(preview:PreviewRunner){this.preview=preview;}
 private async ready(){
  const s=await this.preview.readiness();
  if(!s.ready||!s.build)throw new BuildError('Configuration inspection requires a current successful Preview build.',409);
  return s.build;
 }
 private async run(args:string[],text?:string):Promise<Record<string,unknown>> {
  if(this.active)throw new BuildError('Configuration inspection already active.',409);
  this.active=true;
  try {
   const build=this.preview.build;
   const binary=await checkedPath(build.root,build.profile.outputBinaryRelative);
   return await new Promise((resolve,reject)=>{
    const child=execFile(binary,args,{cwd:build.root,shell:false,encoding:'utf8',timeout:10000,maxBuffer:8*1024*1024,env:{PATH:'/usr/bin:/bin',LANG:'C.UTF-8',OMP_NUM_THREADS:'1',CUDA_VISIBLE_DEVICES:''}},(error,stdout)=>{
     // Core uses exit 3 for structured field validation failures.
     if(error&&error.code!==3){reject(new BuildError('Configuration command failed or exceeded its limits.',502));return;}
     try{const value:unknown=JSON.parse(stdout);if(!value||typeof value!=='object'||Array.isArray(value))throw new Error();resolve(value as Record<string,unknown>);}catch{reject(new BuildError('Invalid Core configuration response.',502));}
    });
    child.stdin?.on('error',()=>undefined);child.stdin?.end(text??'');
   });
  }finally{this.active=false;}
 }
 async schema(){
  const before=await this.ready();const cache=this.schemaCache;const core=cache?.buildId===before.buildId&&cache.sha===before.outputBinary.fingerprint.sha256?cache.core:await this.run(['--config-schema']);const after=await this.ready();
  if(before.buildId!==after.buildId||before.outputBinary.fingerprint.sha256!==after.outputBinary.fingerprint.sha256)throw new BuildError('Build changed during schema request.',409);
  this.schemaCache={buildId:before.buildId,sha:before.outputBinary.fingerprint.sha256,core:validateConfigurationSchema(core)};
  return {protocolVersion:PROTOCOL_VERSION,projectId:this.preview.build.projectId,buildId:before.buildId,binarySha256:before.outputBinary.fingerprint.sha256,core};
 }
 async inspect(r:ConfigurationRequest){
  const keys=['projectId','caseId','configText','configRevision'];
  if(Object.keys(r).length!==keys.length||Object.keys(r).some(k=>!keys.includes(k))||r.projectId!==this.preview.build.projectId||!['Sod','CellularDet'].includes(r.caseId)||typeof r.configText!=='string'||r.configText.includes('\0')||Buffer.byteLength(r.configText)>1024*1024||Buffer.from(r.configText).toString('utf8')!==r.configText||r.configRevision!==createHash('sha256').update(r.configText).digest('hex'))throw new BuildError('Invalid configuration inspection request.');
  const schema=await this.schema();
  const before=await this.ready();const requestId=randomUUID();
  if(schema.buildId!==before.buildId||schema.binarySha256!==before.outputBinary.fingerprint.sha256)throw new BuildError('Build changed after schema request.',409);
  const core=await this.run(['--inspect-config',r.caseId,'--config-stdin','--request-id',requestId],r.configText);
  const pathChecks=await pathPreflight(validateConfigurationSchema(schema.core),validateConfigurationInspection(core,{caseId:r.caseId,configRevision:r.configRevision,requestId}),this.preview.build.root);
  const after=await this.ready();
  if(before.buildId!==after.buildId||before.outputBinary.fingerprint.sha256!==after.outputBinary.fingerprint.sha256)throw new BuildError('Build changed during inspection.',409);
  validateConfigurationInspection(core,{caseId:r.caseId,configRevision:r.configRevision,requestId});
  return {protocolVersion:PROTOCOL_VERSION,identity:{projectId:r.projectId,caseId:r.caseId,configRevision:r.configRevision,requestId,buildId:before.buildId,binarySha256:before.outputBinary.fingerprint.sha256},core,pathChecks};
 }
}
