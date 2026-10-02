import {spawn} from 'node:child_process';
import type {ChildProcess} from 'node:child_process';
import {open,readFile,readdir,rename,writeFile} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {randomUUID} from 'node:crypto';
import {checkedPath,fingerprint,projectRoot} from './files.ts';
import {sameFingerprint,validateFingerprint,readConfig} from './config.ts';
import type {FileFingerprint} from '../src/host/contracts.ts';
export interface RunJob {
 version:'1';runId:string;projectRoot:string;caseId:string;mode:'run'|'restart';
 binaryRelativePath:string;binaryFingerprint:FileFingerprint;
 configRelativePath:string;configFingerprint:FileFingerprint;
 inputRelativePath:string;confirmedBinary:'compiled-version';
 createdAt:string;
}
import type {RunState} from '../src/host/runContracts.ts';
export type {RunState} from '../src/host/runContracts.ts';
async function processTicks(pid:number){
 try{const text=await readFile('/proc/'+pid+'/stat','utf8');return text.slice(text.lastIndexOf(')')+2).split(' ')[19];}catch{return undefined;}
}
async function groupMembers(group:number){
 const result:{pid:number;ticks:string}[]=[];
 for(const entry of await readdir('/proc')){
  if(!/^\d+$/.test(entry))continue;
  const fields=await readFile('/proc/'+entry+'/stat','utf8').then(text=>text.slice(text.lastIndexOf(')')+2).split(' ')).catch(()=>undefined);
  if(fields&&fields[0]!=='Z'&&Number(fields[2])===group)result.push({pid:Number(entry),ticks:fields[19]});
 }
 return result;
}
async function finishOwnedStop(members:{pid:number;ticks:string}[],deadline:number){
 // The group leader may exit before a TERM-ignoring child. Keep the identities
 // captured while we owned the group, rather than signal a potentially reused PGID.
 let remaining=members;
 while(remaining.length){
  remaining=(await Promise.all(remaining.map(async member=>{
   const fields=await readFile('/proc/'+member.pid+'/stat','utf8').then(text=>text.slice(text.lastIndexOf(')')+2).split(' ')).catch(()=>undefined);
   return fields&&fields[0]!=='Z'&&fields[19]===member.ticks?member:undefined;
  }))).filter((member):member is {pid:number;ticks:string}=>!!member);
  if(!remaining.length)return;
  if(Date.now()>=deadline){
   for(const member of remaining)if(await processTicks(member.pid)===member.ticks)
    try{process.kill(member.pid,'SIGKILL');}catch(e){if((e as NodeJS.ErrnoException).code!=='ESRCH')throw e;}
   return;
  }
  await new Promise(resolve=>setTimeout(resolve,50));
 }
}
async function store(directory:string,state:RunState){
 const temp=path.join(directory,'.state-'+randomUUID()+'.tmp');
 await writeFile(temp,JSON.stringify(state,null,2)+'\n',{flag:'wx',mode:0o600});
 await rename(temp,path.join(directory,'state.json'));
}
function validateJob(job:RunJob){
 if(job.version!=='1'||!/^[-a-f0-9]{36}$/.test(job.runId)||!['run','restart'].includes(job.mode)||
  job.confirmedBinary!=='compiled-version'||!path.isAbsolute(job.projectRoot)||!/^[A-Za-z][A-Za-z0-9_]*$/.test(job.caseId))
  throw new Error('Invalid confirmed run identity.');
 validateFingerprint(job.binaryFingerprint);validateFingerprint(job.configFingerprint);
}
/** Runs in the independent terminal, never in the Studio/Preview process group. */
export async function executeRun(job:RunJob,directory:string):Promise<RunState>{
 validateJob(job);
 if(process.platform!=='linux'||await projectRoot(job.projectRoot)!==job.projectRoot)
  throw new Error('Run requires the selected Linux project.');
 const expected='studio/.local/runs/'+job.runId;
 if(await checkedPath(job.projectRoot,expected)!==directory||job.inputRelativePath!==expected+'/input.par')
  throw new Error('Run directory does not match the owned run identity.');
 const state:RunState={runId:job.runId,state:'starting',workerPid:process.pid,startedAt:new Date().toISOString()};
 const claim=await open(path.join(directory,'worker.lock'),'wx',0o600);await claim.close();
 await store(directory,state);
 let owned:ChildProcess|undefined;
 let output:Awaited<ReturnType<typeof open>>|undefined,tail:Awaited<ReturnType<typeof open>>|undefined;
 try{
  if((await readFile(directory+'/stop-request','utf8').catch(()=>'' )).trim()===job.runId){
   state.state='stopped';return state;
  }
  const original=await readConfig(job.projectRoot,job.configRelativePath,job.runId);
  if(!sameFingerprint(original.fingerprint,job.configFingerprint))throw new Error('Saved configuration changed after confirmation.');
  const input=await readConfig(job.projectRoot,job.inputRelativePath,job.runId);
  if(input.fingerprint.sha256!==job.configFingerprint.sha256||input.fingerprint.size!==job.configFingerprint.size)
   throw new Error('Run input copy does not match the confirmed saved bytes.');
  const binary=await fingerprint(job.projectRoot,job.binaryRelativePath,'executable');
  if(!binary.exists||binary.error||!binary.sha256||binary.size===undefined||!binary.modifiedTime||
   !sameFingerprint({sha256:binary.sha256,size:binary.size,modifiedTime:binary.modifiedTime},job.binaryFingerprint))
   throw new Error('Selected binary changed after confirmation.');
  const executable=await checkedPath(job.projectRoot,job.binaryRelativePath);
  const inputPath=await checkedPath(job.projectRoot,job.inputRelativePath);
  output=await open(path.join(directory,'console.log'),'wx',0o600);
  tail=await open(path.join(directory,'console.log'),'r');
  // Direct file descriptors avoid a broken GUI/terminal stdout pipe killing Core.
  const child=spawn(executable,[job.caseId,inputPath],{cwd:job.projectRoot,detached:true,shell:false,
   env:{PATH:'/usr/bin:/bin',HOME:process.env.HOME??'',LANG:'C.UTF-8'},
   stdio:['ignore',output.fd,output.fd]});
  owned=child;
  let ended=false,spawnError:Error|undefined,code:number|null=null,signal:NodeJS.Signals|null=null;
  child.once('error',e=>{spawnError=e;ended=true;});
  child.once('close',(c,s)=>{code=c;signal=s;ended=true;});
  state.processId=child.pid;state.processStartTicks=child.pid?await processTicks(child.pid):undefined;
  if(state.processStartTicks)state.processIdentity='captured';
  state.state='running';await store(directory,state);
  let stopMembers:{pid:number;ticks:string}[]=[];
  let stopped=false,killAt=0,position=0,consoleAvailable=true,consoleReady=true;
  const consoleError=()=>{consoleAvailable=false;};
  process.stdout.on('error',consoleError);
  const forward=async()=>{
   if(!consoleAvailable||!consoleReady)return;
   const buffer=Buffer.alloc(65536),result=await tail!.read(buffer,0,buffer.length,position);position+=result.bytesRead;
   if(result.bytesRead&&!process.stdout.write(buffer.subarray(0,result.bytesRead))){
    consoleReady=false;process.stdout.once('drain',()=>{consoleReady=true;});
   }
  };
  try{
   while(!ended){
    await forward();
    const stop=await readFile(path.join(directory,'stop-request'),'utf8').catch(()=>'');
    if(stop.trim()===job.runId&&!stopped){stopped=true;killAt=Date.now()+2000;
     if(child.pid&&state.processStartTicks&&await processTicks(child.pid)===state.processStartTicks){
      stopMembers=await groupMembers(child.pid);
      try{process.kill(-child.pid,'SIGTERM');}catch(e){if((e as NodeJS.ErrnoException).code!=='ESRCH')throw e;}
     }
    }
    if(stopped&&Date.now()>=killAt&&child.pid&&state.processStartTicks&&await processTicks(child.pid)===state.processStartTicks)
     try{process.kill(-child.pid,'SIGKILL');}catch(e){if((e as NodeJS.ErrnoException).code!=='ESRCH')throw e;}
    await new Promise(resolve=>setTimeout(resolve,100));
   }
   if(stopped)await finishOwnedStop(stopMembers,killAt);
   await forward();
  }finally{process.stdout.off('error',consoleError);}
  if(spawnError)throw spawnError;
  state.state=stopped?'stopped':code===0?'succeeded':'failed';
  state.exitCode=code;state.signal=signal;
  if(!state.processStartTicks)state.processIdentity='exited-before-observation';
 }catch(e){
  // A supervisor failure must not silently abandon its live Core process.
  if(owned?.pid&&state.processStartTicks&&await processTicks(owned.pid)===state.processStartTicks){
   const members=await groupMembers(owned.pid);
   try{process.kill(-owned.pid,'SIGTERM');}catch{/* process may already have exited */}
   await finishOwnedStop(members,Date.now()+2000);
  }
  state.state='failed';state.error=e instanceof Error?e.message:'Run failed';
 }
 finally{
  await tail?.close();await output?.close();
  state.finishedAt=new Date().toISOString();await store(directory,state);
 }
 return state;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const directory=path.resolve(process.argv[2]??'');
 try{
  const job=JSON.parse(await readFile(path.join(directory,'job.json'),'utf8')) as RunJob;
  const state=await executeRun(job,directory);
  console.log('\nARCH run '+state.runId+': '+state.state+'\nLog: '+path.join(directory,'console.log'));
  process.exitCode=state.state==='succeeded'?0:1;
 }catch(e){console.error(e instanceof Error?e.message:String(e));process.exitCode=1;}
}
