/** Module-only Windows Run adapters; tool paths are Host bootstrap, never job.json. */
import {spawn} from 'node:child_process';
import type {ChildProcess,SpawnOptions} from 'node:child_process';
import {windowsExecutable,windowsEnvironment,windowsProcessIdentity as queryIdentity,windowsSupervisionArgs,spawnWindowsSupervised,captureOwned} from './hostProcess.ts';
import type {WindowsProcessTools} from './hostProcess.ts';
import type {WindowsHostTools} from './hostProcess.ts';
import type {WindowsRunProcessIdentity} from '../src/host/runContracts.ts';

/** The Run adapter needs ownership and an optional bounded PATH, not a command. */
export type WindowsRunTools = Pick<WindowsHostTools,'supervisor'>&{path?:string;terminal?:string};

export const validateWindowsExecutable=windowsExecutable;
const executable=windowsExecutable;
export function requireWindowsRunTools(value:WindowsRunTools|undefined):WindowsRunTools{
 if(process.platform!=='win32')throw new Error('Native Windows Run tools are only valid on Windows.');
 if(!value)throw new Error('Native Windows Run supervisor is not configured.');
 executable(value.supervisor);
 if(value.terminal!==undefined)executable(value.terminal);
 if(value.path!==undefined&&(typeof value.path!=='string'||value.path.includes('\0')||value.path.length>32767))
  throw new Error('Invalid native Windows Run PATH.');
 return value;
}
function processTools(tools:WindowsRunTools):WindowsProcessTools {
 const value=requireWindowsRunTools(tools),candidate=process.env.SystemRoot??process.env.WINDIR;
 const root=candidate&&/^[A-Za-z]:[\\/]/.test(candidate)?candidate:'C:\\Windows';
 return {supervisor:value.supervisor,path:value.path??(root+'\\System32;'+root)};
}
export function windowsRunEnvironment(tools:WindowsRunTools){return windowsEnvironment(processTools(tools));}
/** Stable identity uses the common process owner, with only a Run record tag added. */
export function windowsProcessIdentity(tools:WindowsRunTools,pid:number):WindowsRunProcessIdentity&{alive:boolean}{
 return {version:'windows-v1',...queryIdentity(processTools(tools),pid)};
}
export function requireWindowsProcessIdentity(tools:WindowsRunTools,expected:WindowsRunProcessIdentity){
 if(expected.version!=='windows-v1')throw new Error('Windows process identity version unavailable.');
 const actual=windowsProcessIdentity(tools,expected.pid);
 if(!actual.alive||actual.creationTime!==expected.creationTime)
  throw new Error('Windows process identity changed or no longer alive; outcome unknown.');
 return actual;
}
export const windowsSupervisorRunArgs=windowsSupervisionArgs;
/** Reuse the common Job implementation, adding only output lease keys. */
export function spawnWindowsOwned(program:string,args:readonly string[],options:SpawnOptions,
 parent:WindowsRunProcessIdentity,tools:WindowsRunTools,lockKeys:readonly string[]=[]):ChildProcess{
 return spawnWindowsSupervised(program,args,options,processTools(tools),parent,lockKeys,undefined,true);
}
export async function createPrivateRunDirectory(root:string,runId:string,tools:WindowsRunTools){
 const selected=processTools(tools);
 const result=await captureOwned(selected.supervisor,['run-directory',root,runId],'',root,selected);
 if(result.code!==0||result.stdout.trim()!=='{"created":true}')throw new Error('Private native Run directory creation failed.');
}
/** New console plus Job breakaway keeps the worker independent from the Host process tree. */
export function spawnWindowsConsoleRun(program:string,args:readonly string[],tools:WindowsRunTools,options:SpawnOptions):ChildProcess{
 if(options.shell)throw new Error('Shell execution is not supported.');
 const value=requireWindowsRunTools(tools);
 if(value.terminal){
  // wt has its own semicolon command grammar. Only the fixed program/script
  // paths are literal; all project/tool bootstrap data travels as base64url.
  if([program,...args].some(arg=>arg.includes(';')||arg.includes('\0')))throw new Error('Windows Terminal command delimiter in fixed executable path.');
  return spawn(executable(value.terminal),['-w','new','new-tab','--title','ARCH Run','--suppressApplicationTitle',executable(program),...args],{
   ...options,shell:false,detached:true,windowsHide:false,env:windowsRunEnvironment(value)});
 }
 return spawn(executable(value.supervisor),['console','--',executable(program),...args],{
  ...options,shell:false,detached:true,windowsHide:false,env:windowsRunEnvironment(value)});
}
/** Host-created argv for the worker terminal; mutable job.json carries no executable paths. */
export function windowsWorkerBootstrap(args:string[]):WindowsRunTools|undefined{
 if(!args.length)return undefined;
 if(args.length!==2||args[0]!=='--windows-supervisor')throw new Error('Invalid trusted Windows worker bootstrap.');
 // The console helper supplies a fixed environment. PATH is only a bounded
 // fallback for Core/tool lookup; executables still come from validated argv.
 return requireWindowsRunTools({supervisor:args[1],path:process.env.PATH});
}

/** Internal argv contains no raw project paths, quotes or terminal delimiters. */
export function windowsRunBootstrap(directory:string,tools:WindowsRunTools){
 const selected=processTools(tools);
 return ['--run-bootstrap',Buffer.from(JSON.stringify({directory,windowsTools:selected}),'utf8').toString('base64url')];
}
export function parseWindowsRunBootstrap(args:string[]){
 if(args.length!==2||args[0]!=='--run-bootstrap'||!/[A-Za-z0-9_-]+/.test(args[1])||args[1].length>65536)throw new Error('Invalid native Run bootstrap.');
 const value=JSON.parse(Buffer.from(args[1],'base64url').toString('utf8')) as {directory:string;windowsTools:WindowsRunTools};
 if(!value||typeof value.directory!=='string'||!/^[A-Za-z]:[\\/]/.test(value.directory)||value.directory.includes('\0'))throw new Error('Invalid native Run directory.');
 return {directory:value.directory,windowsTools:processTools(value.windowsTools)};
}
