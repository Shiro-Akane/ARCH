import {verifyRunSupervisor} from './runSupervisor.ts';
import {spawn} from 'node:child_process';
import {access,mkdir,open,opendir,readFile,writeFile} from 'node:fs/promises';
import {constants} from 'node:fs';
import {randomUUID} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {checkedPath} from './files.ts';
import {BuildError} from './buildRunner.ts';
import type {RunPreparationRunner,ConfirmRunRequest} from './runPreparation.ts';
import type {RunJob,RunState} from './runWorker.ts';
interface TerminalHandle {pid:number;exited:()=>boolean}
export interface RunControllerHooks {
 /** Module-only test seam; never accepted by HTTP or a browser request. */
 terminal?:(directory:string,runId:string)=>Promise<TerminalHandle>;
}
export class RunController {
 private preparing:RunPreparationRunner;private hooks:RunControllerHooks;private launching=false;
 constructor(preparing:RunPreparationRunner,hooks:RunControllerHooks={}){this.preparing=preparing;this.hooks=hooks;}
 isLaunching(){return this.launching;}
 private async directory(runId:string){
  if(!/^[a-f0-9]{8}-[a-f0-9-]{27}$/.test(runId))throw new BuildError('Invalid run ID.');
  try{return await checkedPath(this.preparing.root,'studio/.local/runs/'+runId);}
  catch(e){if((e as NodeJS.ErrnoException).code==='ENOENT')throw new BuildError('Run not found.',404);throw e;}
 }
 private async terminal(directory:string,runId:string):Promise<TerminalHandle>{
  const stderr=await open(directory+'/terminal.log','wx',0o600);
  try{
   const environment:Record<string,string>={PATH:'/usr/bin:/bin',HOME:process.env.HOME??'',LANG:'C.UTF-8'};
   for(const key of ['DISPLAY','XAUTHORITY','XDG_RUNTIME_DIR','WAYLAND_DISPLAY','DBUS_SESSION_BUS_ADDRESS'])
    if(process.env[key])environment[key]=process.env[key]!;
   const child=spawn('/usr/bin/xterm',['-hold','-fa','Monospace','-fs','11','-T','ARCH '+runId,'-e',
    process.execPath,fileURLToPath(new URL('./runWorker.ts',import.meta.url)),directory],{
     cwd:this.preparing.root,shell:false,detached:true,stdio:['ignore',stderr.fd,stderr.fd],env:environment});
   await new Promise<void>((resolve,reject)=>{child.once('spawn',resolve);child.once('error',reject);});
   child.unref();
   return {pid:child.pid!,exited:()=>child.exitCode!==null||child.signalCode!==null};
  }finally{await stderr.close();}
 }
 async start(request:ConfirmRunRequest){
  if(this.launching)throw new BuildError('Run handoff already active.',409);
  this.launching=true;
  try{
   if(!this.hooks.terminal){
    if(process.platform!=='linux'||!process.env.DISPLAY)throw new BuildError('Linux display unavailable; independent Run terminal required.',409);
    try{await access('/usr/bin/xterm',constants.X_OK);}catch{throw new BuildError('Install xterm in the Linux project environment before Run/Restart.',409);}
   }
   const confirmed=await this.preparing.consume(request),runId=randomUUID(),root=this.preparing.root;
   for(const relative of ['studio/.local','studio/.local/runs']){
    await mkdir(root+'/'+relative,{mode:0o700}).catch((e:NodeJS.ErrnoException)=>{if(e.code!=='EEXIST')throw e;});
    await checkedPath(root,relative);
   }
   await mkdir(root+'/studio/.local/runs/'+runId,{mode:0o700});
   const directory=await this.directory(runId),plan=confirmed.plan;
   const job:RunJob={version:'1',runId,projectRoot:root,caseId:plan.caseId,mode:plan.mode,
    binaryRelativePath:plan.binary.relativePath,binaryFingerprint:plan.binary.fingerprint,
    configRelativePath:plan.config.relativePath,configFingerprint:plan.config.fingerprint,
    inputRelativePath:'studio/.local/runs/'+runId+'/input.par',confirmedBinary:'compiled-version',createdAt:new Date().toISOString(),checkpoint:confirmed.checkpoint,outputDirectories:confirmed.outputDirectories};
   await writeFile(directory+'/input.par',confirmed.configText,{flag:'wx',mode:0o400});
   await writeFile(directory+'/job.json',JSON.stringify(job,null,2),{flag:'wx',mode:0o600});
   await writeFile(directory+'/confirmation.json',JSON.stringify({request,confirmedAt:job.createdAt,plan},null,2),{flag:'wx',mode:0o600});
   let terminal:TerminalHandle;
   try{terminal=await (this.hooks.terminal??((d,id)=>this.terminal(d,id)))(directory,runId);}
   catch(e){await writeFile(directory+'/stop-request',runId,{flag:'wx',mode:0o600});throw e;}
   await writeFile(directory+'/terminal.json',JSON.stringify({pid:terminal.pid,runId}),{flag:'wx',mode:0o600});
   const deadline=Date.now()+10000;
   while(Date.now()<deadline){
    const state=await this.status(runId).catch(()=>undefined);
    if(state)return {projectId:this.preparing.projectId,runId,terminalPid:terminal.pid,state};
    if(terminal.exited())break;
    await new Promise(resolve=>setTimeout(resolve,50));
   }
   // No blind retry: a delayed terminal may still own this job. Preserve the ID.
   await writeFile(directory+'/stop-request',runId,{flag:'wx',mode:0o600});
   throw new BuildError('Terminal handoff unconfirmed; Stop requested for run '+runId+'. Inspect its retained state before retrying.',502);
  }finally{this.launching=false;}
 }
 async status(runId:string):Promise<RunState>{
  const directory=await this.directory(runId);
  const file=await open(await checkedPath(this.preparing.root,'studio/.local/runs/'+runId+'/state.json'),constants.O_RDONLY|constants.O_NOFOLLOW);
  try{
   const size=(await file.stat()).size;if(size>16384)throw new BuildError('Invalid run state.',502);
   const state=JSON.parse(await file.readFile('utf8')) as RunState;
   if(state.runId!==runId||!['starting','running','succeeded','failed','stopped'].includes(state.state)||!Number.isInteger(state.workerPid)||state.workerPid<=0)
    throw new BuildError('Run state identity mismatch.',502);
   const job=JSON.parse(await readFile(directory+'/job.json','utf8')) as RunJob;
   if(job.runId!==runId||job.projectRoot!==this.preparing.root)throw new BuildError('Run owner mismatch.',409);
   await verifyRunSupervisor(state);
   return state;
  }finally{await file.close();}
 }
 async history(){
  const root=this.preparing.root,entries:string[]=[];
  let directory:string;
  try{directory=await checkedPath(root,'studio/.local/runs');}
  catch(e){if((e as NodeJS.ErrnoException).code==='ENOENT')return [];throw e;}
  const handle=await opendir(directory);
  for await(const entry of handle){
   if(!entry.isDirectory()||!/^[a-f0-9]{8}-[a-f0-9-]{27}$/.test(entry.name))continue;
   entries.push(entry.name);
   if(entries.length>1000)throw new BuildError('Run history exceeds 1000 records; history is unavailable rather than silently truncated.',413);
  }
  const records=[];
  for(const runId of entries){
   try{
    const file=await open(await checkedPath(root,'studio/.local/runs/'+runId+'/job.json'),constants.O_RDONLY|constants.O_NOFOLLOW);
    let job:RunJob;
    try{if((await file.stat()).size>16384)throw new Error('Oversized run job');job=JSON.parse(await file.readFile('utf8')) as RunJob;}
    finally{await file.close();}
    if(job.runId!==runId||job.projectRoot!==root||job.version!=='1'||typeof job.caseId!=='string'||typeof job.configRelativePath!=='string'||typeof job.createdAt!=='string'||!job.configFingerprint||!/^[a-f0-9]{64}$/.test(job.configFingerprint.sha256))
     throw new Error('Invalid saved run identity');
    let state:RunState|null=null,diagnostic:string|null=null;
    try{state=await this.status(runId);}catch(e){diagnostic=e instanceof Error?e.message:'Run status unavailable';}
    records.push({runId,caseId:job.caseId,configPath:job.configRelativePath,configSha:job.configFingerprint.sha256,createdAt:job.createdAt,state,diagnostic});
   }catch(e){records.push({runId,diagnostic:e instanceof Error?e.message:'Run record unreadable',state:null});}
  }
  return records.sort((a,b)=>(b.createdAt??'').localeCompare(a.createdAt??'')||a.runId.localeCompare(b.runId));
 }
 async stop(runId:string){
  const state=await this.status(runId);
  if(!['starting','running'].includes(state.state))throw new BuildError('Run already finished.',409);
  const directory=await this.directory(runId);
  await writeFile(directory+'/stop-request',runId,{flag:'wx',mode:0o600}).catch(async(e:NodeJS.ErrnoException)=>{
   if(e.code!=='EEXIST'||await readFile(directory+'/stop-request','utf8')!==runId)throw e;
  });
  return {runId,stopRequested:true};
 }
}
