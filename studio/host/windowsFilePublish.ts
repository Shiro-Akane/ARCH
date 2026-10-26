/** Windows handles only; each existing caller retains its content and validation. */
import {randomUUID} from 'node:crypto';
import type {ChildProcess} from 'node:child_process';
import {spawnOwned,terminateOwned} from './hostProcess.ts';
import type {WindowsHostTools} from './hostProcess.ts';

export class WindowsFileError extends Error {
 readonly committed: boolean | undefined;
 readonly win32: number | undefined;
 constructor(message:string,stderr:string){
  super(message+' '+stderr);
  const value=/committed=(true|false)/.exec(stderr);
  this.committed=value?value[1]==='true':undefined;
  const code=/win32=(\d+)/.exec(stderr);this.win32=code?Number(code[1]):undefined;
 }
}
interface FileOperation {
 child:ChildProcess;
 stage:(name:string)=>Promise<void>;
 finished:Promise<void>;
 abort:()=>Promise<void>;
}
function operation(args:string[],tools:WindowsHostTools|undefined,success:string,timeout:number,requiredStages:readonly string[]=[]):FileOperation {
 if(!tools?.publisher)throw new Error('Trusted native Windows file helper required.');
 const child=spawnOwned(tools.publisher,args,{shell:false,stdio:['pipe','pipe','pipe']},tools);
 let stdout='',stderr='',overflow=false,timedOut=false,settled=false,protocolError=false;
 const stages=new Set<string>(),waiters=new Map<string,{resolve:()=>void;reject:(error:Error)=>void}>();
 let pending='';
 const timer=setTimeout(()=>{timedOut=true;terminateOwned(child);},timeout);
 const finished=new Promise<void>((resolve,reject)=>{
  child.once('error',reject);
  child.once('close',code=>{
   settled=true;clearTimeout(timer);
   const error=new WindowsFileError('Native file operation failed; publication outcome must be inspected.',stderr);
   for(const waiter of waiters.values())waiter.reject(error);waiters.clear();
   const transcript=[...requiredStages.map(name=>JSON.stringify({stage:name})),success].join('\n');
   if(!overflow&&!timedOut&&!protocolError&&code===0&&pending===''&&stdout.trim().replaceAll('\r\n','\n')===transcript)resolve();
   else reject(error);
  });
 });
 // Stage failures may occur before the content owner begins awaiting completion.
 void finished.catch(()=>undefined);
 child.stdout!.on('data',(chunk:Buffer)=>{
  stdout+=chunk.toString('utf8');pending+=chunk.toString('utf8');
  if(Buffer.byteLength(stdout)>4096){overflow=true;terminateOwned(child);return;}
  let end:number;
  while((end=pending.indexOf('\n'))>=0){
   const line=pending.slice(0,end).replace(/\r$/,'');pending=pending.slice(end+1);
   const match=/^\{"stage":"(created|prepared|held)"\}$/.exec(line);
   if(match){if(timedOut||overflow)return;if(requiredStages[stages.size]!==match[1]){protocolError=true;terminateOwned(child);return;}if(match[1]==='held')clearTimeout(timer);stages.add(match[1]);waiters.get(match[1])?.resolve();waiters.delete(match[1]);}
  }
 });
 child.stderr!.on('data',(chunk:Buffer)=>{stderr+=chunk.toString('utf8');if(Buffer.byteLength(stderr)>4096){overflow=true;terminateOwned(child);}});
 child.stdin!.on('error',()=>undefined); // close and the native commit receipt own the outcome
 return {child,finished,stage:name=>{
  if(stages.has(name))return Promise.resolve();
  if(settled)return finished.then(()=>{throw new Error('Native file stage missing.');});
  return new Promise<void>((resolve,reject)=>waiters.set(name,{resolve,reject}));
 },abort:async()=>{
  child.stdin!.end();
  if(!settled){
   // EOF makes a prepared helper delete its owned temp; kill only after a grace period.
   const grace=setTimeout(()=>terminateOwned(child),2000);
   await finished.catch(()=>undefined);clearTimeout(grace);
  }
 }};
}

export async function publishWindowsManifest(directory:string,target:string,content:string,tools?:WindowsHostTools){
 if(Buffer.byteLength(content)>4*1024*1024)throw new Error('Build Manifest exceeds publication byte budget.');
 const op=operation([directory,target,'.build-'+randomUUID()+'.tmp'],tools,'{"committed":true}',15000);
 op.child.stdin!.end(content);await op.finished;
}

export async function publishWindowsConfig(directory:string,target:string,text:string,createOnly:boolean,tools:WindowsHostTools|undefined,
 beforeWrite:()=>Promise<void>,beforeCommit:()=>Promise<void>){
 const bytes=Buffer.from(text,'utf8');
 if(bytes.length>1024*1024)throw new Error('Configuration exceeds 1 MiB.');
 const op=operation([createOnly?'config-create':'config-save',directory,target,'.arch-config-'+randomUUID()+'.tmp',String(bytes.length)],tools,'{"committed":true}',15000,['created','prepared']);
 try{
  await op.stage('created');await beforeWrite();op.child.stdin!.write(bytes);
  await op.stage('prepared');await beforeCommit();op.child.stdin!.end('!');
  await op.finished;
 }catch(error){await op.abort();throw error;}
}

/** Prevent writes, replacement and ancestor rebinding while the original HDF reader opens the path. */
export async function holdWindowsRead(directory:string,target:string,tools?:WindowsHostTools){
 const op=operation(['hold-read',directory,target],tools,'{"released":true}',15000,['held']);
 try{await op.stage('held');}catch(error){await op.abort();throw error;}
 return {close:async()=>{op.child.stdin!.end('!');await op.finished;}};
}

/** A configured path alone is not evidence that an older helper supports config writes. */
export async function windowsConfigAvailable(tools?:WindowsHostTools){
 try{
  const op=operation(['capabilities'],tools,'{"protocolVersion":"windows-file-1","manifest":true,"config":true,"holdRead":true}',3000);
  op.child.stdin!.end();await op.finished;return true;
 }catch{return false;}
}
