/** OS process ownership shared by existing runners; no workflow or file writer. */
import path from 'node:path';
import {execFileSync, spawn} from 'node:child_process';
import type {ChildProcess, SpawnOptions} from 'node:child_process';

/** Trusted Host setup only. Never populated from HTTP or a project data file. */
export interface WindowsProcessTools {supervisor:string;path:string}
export interface WindowsHostTools extends WindowsProcessTools {cmake:string;ninja?:string;objdump?:string;publisher?:string;rc?:string;terminal?:string}
export interface ProcessIdentity {pid:number;creationTime:string;alive:boolean}
export function windowsExecutable(value:unknown):string{
 if(typeof value!=='string'||!/^[A-Za-z]:[\\/]/.test(value)||!path.win32.isAbsolute(value)||value.includes('\0')||!value.toLowerCase().endsWith('.exe'))throw new Error('Absolute local Windows .exe required.');
 return value;
}
const executable=windowsExecutable;
export function windowsEnvironment(tools:WindowsProcessTools):NodeJS.ProcessEnv {
 if(process.platform!=='win32'||!tools.path||tools.path.includes('\0')||tools.path.length>32767)throw new Error('Native Windows Host tools required.');
 executable(tools.supervisor);
 const host=tools as Partial<WindowsHostTools>;
 for(const value of [host.cmake,host.ninja,host.objdump,host.publisher,host.rc,host.terminal])if(value!==undefined)executable(value);
 const env:NodeJS.ProcessEnv={PATH:tools.path,PATHEXT:'.COM;.EXE;.BAT;.CMD'};
 for(const key of ['SystemRoot','WINDIR','ComSpec','TEMP','TMP','USERPROFILE'])if(process.env[key])env[key]=process.env[key];
 return env;
}
/** Creation time and liveness come from one opened process handle, never PID alone. */
export function windowsProcessIdentity(tools:WindowsProcessTools,pid:number):ProcessIdentity {
 if(!Number.isSafeInteger(pid)||pid<=0)throw new Error('Invalid process PID.');
 const value:unknown=JSON.parse(execFileSync(executable(tools.supervisor),['identity',String(pid)],{env:windowsEnvironment(tools),encoding:'utf8',timeout:3000,maxBuffer:4096,windowsHide:true}));
 const identity=value as ProcessIdentity;
 if(!identity||identity.pid!==pid||typeof identity.creationTime!=='string'||!/^\d{1,20}$/.test(identity.creationTime)||typeof identity.alive!=='boolean')throw new Error('Process identity unavailable.');
 return identity;
}
export function requireWindowsProcess(tools:WindowsProcessTools,expected:ProcessIdentity){
 const actual=windowsProcessIdentity(tools,expected.pid);
 if(!actual.alive||!expected.alive||actual.creationTime!==expected.creationTime)throw new Error('Process identity changed or no longer alive; outcome unknown.');
 return actual;
}
/** The ChildProcess identifies the owner/helper. Its Job contains the actual tool. */
export function spawnOwned(program:string,args:readonly string[],options:SpawnOptions,tools?:WindowsProcessTools,temporaryDirectory?:string):ChildProcess {
 if(options.shell)throw new Error('Shell execution is not supported.');
 if(process.platform==='win32'){
  if(!tools)throw new Error('Native Windows process supervisor is not configured.');
  const parent=windowsProcessIdentity(tools,process.pid);
  if(!parent.alive)throw new Error('Host process identity unavailable.');
  return spawnWindowsSupervised(program,args,options,tools,parent,[],temporaryDirectory);
 }
 if(process.platform!=='linux')throw new Error('Unsupported Host process platform.');
 return spawn(program,[...args],{...options,shell:false,detached:true});
}
/** One argv grammar for Configure/Build/queries and output-leased production runs. */
export function windowsSupervisionArgs(parent:Pick<ProcessIdentity,'pid'|'creationTime'>,program:string,args:readonly string[],lockKeys:readonly string[]=[],controlled=false){
 if(!Number.isSafeInteger(parent.pid)||parent.pid<=0||!/^\d{1,20}$/.test(parent.creationTime))throw new Error('Invalid parent identity.');
 if(lockKeys.length>16||lockKeys.some(key=>!/[a-f0-9]{64}/.test(key)||key.length!==64))throw new Error('Invalid Windows output lock key.');
 const argv=['run',String(parent.pid),parent.creationTime];
 for(const key of [...new Set(lockKeys)].sort())argv.push('--lock-key',key);
 if(controlled)argv.push('--cancel-stdin');
 return [...argv,'--',executable(program),...args];
}
export function spawnWindowsSupervised(program:string,args:readonly string[],options:SpawnOptions,tools:WindowsProcessTools,
 parent:Pick<ProcessIdentity,'pid'|'creationTime'>,lockKeys:readonly string[]=[],temporaryDirectory?:string,controlled=false){
 if(options.shell)throw new Error('Shell execution is not supported.');
 const env=windowsEnvironment(tools);
 if(options.env?.OMP_NUM_THREADS==='1')env.OMP_NUM_THREADS='1';
 if(options.env?.CUDA_VISIBLE_DEVICES==='')env.CUDA_VISIBLE_DEVICES='';
 if(temporaryDirectory){if(!/^[A-Za-z]:[\\/]/.test(temporaryDirectory)||temporaryDirectory.includes('\0'))throw new Error('Absolute retained temp directory required.');env.TMPDIR=temporaryDirectory;env.TEMP=temporaryDirectory;env.TMP=temporaryDirectory;}
 return spawn(executable(tools.supervisor),windowsSupervisionArgs(parent,program,args,lockKeys,controlled),{...options,shell:false,detached:false,windowsHide:true,env});
}
/** Windows kills the owned helper handle; Job close terminates its whole subtree. */
export function terminateOwned(child:ChildProcess,signal:NodeJS.Signals='SIGTERM'){
 if(!child.pid)return;
 if(process.platform==='win32'){
  if(child.exitCode===null&&child.signalCode===null)child.kill(signal);
  return;
 }
 try{process.kill(-child.pid,signal);}catch(e){if((e as NodeJS.ErrnoException).code!=='ESRCH')throw e;}
}

/** Capture one owned native request; failure settles only after the Job owner exits. */
export interface CaptureLimits {timeoutMs?:number;stdoutBytes?:number;stderrBytes?:number;signal?:AbortSignal}
export async function captureOwned(program:string,args:string[],text:string,cwd:string,tools?:WindowsProcessTools,limits:CaptureLimits={}){
 const timeout=limits.timeoutMs??10000,stdoutLimit=limits.stdoutBytes??8*1024*1024,stderrLimit=limits.stderrBytes??8*1024*1024;
 if(!Number.isSafeInteger(timeout)||timeout<1||timeout>10000||
  !Number.isSafeInteger(stdoutLimit)||stdoutLimit<1||stdoutLimit>8*1024*1024||
  !Number.isSafeInteger(stderrLimit)||stderrLimit<1||stderrLimit>8*1024*1024)
  throw new Error('Capture bounds may only tighten the existing command limits.');
 if(limits.signal?.aborted)throw new Error('Core command cancelled.');
 const started=performance.now();
 const child=spawnOwned(program,args,{cwd,shell:false,stdio:['pipe','pipe','pipe'],env:{OMP_NUM_THREADS:'1',CUDA_VISIBLE_DEVICES:''}},tools);
 return new Promise<{stdout:string;code:number|null}>((resolve,reject)=>{
  const output:Buffer[]=[];let bytes=0,errors=0,failure:Error|undefined;
  const stop=(error:Error)=>{failure??=error;terminateOwned(child,'SIGKILL');};
  const timer=setTimeout(()=>stop(new Error('Core command exceeded its wall-clock limit.')),Math.max(0,timeout-(performance.now()-started)));
  const cancel=()=>stop(new Error('Core command cancelled.'));
  limits.signal?.addEventListener('abort',cancel,{once:true});
  if(limits.signal?.aborted)cancel();
  child.stdout!.on('data',(chunk:Buffer)=>{bytes+=chunk.length;if(bytes>stdoutLimit)stop(new Error('Core response exceeds its byte limit.'));else output.push(chunk);});
  child.stderr!.on('data',(chunk:Buffer)=>{errors+=chunk.length;if(errors>stderrLimit)stop(new Error('Core diagnostics exceed their byte limit.'));});
  child.once('error',error=>{failure??=error;});
  child.once('close',code=>{clearTimeout(timer);limits.signal?.removeEventListener('abort',cancel);if(failure)reject(failure);else resolve({stdout:Buffer.concat(output).toString('utf8'),code});});
  child.stdin!.on('error',()=>undefined);child.stdin!.end(text);
 });
}
