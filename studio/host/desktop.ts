/** Packaged desktop entrypoint. Only Electron main supplies this launch envelope. */
import {createHostServer,listenLocal} from './server.ts';
import {openDesktopProject} from './desktopProject.ts';
import {windowsEnvironment} from './hostProcess.ts';
import type {WindowsHostTools} from './hostProcess.ts';
async function main(){
const launch=JSON.parse(Buffer.from(process.argv[2]??'', 'base64url').toString('utf8')) as {project?:string;cwd:string;source?:string;config?:string;binary?:string;caseId?:string;buildDir?:string;origin:string;token:string;windowsTools?:WindowsHostTools};
if(!/^[a-f0-9]{64}$/.test(launch.token))throw new Error('Invalid desktop ownership token.');
const windowsTools=launch.windowsTools;
if(process.platform==='win32'&&!windowsTools)throw new Error('Trusted Windows desktop tools are required.');
if(windowsTools){
 if(typeof windowsTools!=='object'||Array.isArray(windowsTools)||Object.keys(windowsTools).some(key=>!['cmake','supervisor','path','ninja','objdump','publisher','rc','terminal'].includes(key)))throw new Error('Invalid private Windows desktop tool context.');
 windowsEnvironment(windowsTools);
}
const {root,reader,caseId,config,selectedSource}=await openDesktopProject(launch,{windowsTools});
const server=createHostServer(reader,launch.origin,{token:launch.token});
await listenLocal(server,0);
const port=(server.address() as {port:number}).port;
console.log(JSON.stringify({kind:'desktop-ready',port,pid:process.pid,project:root,caseId,config,selectedSource,token:launch.token}));
let closing=false;
async function close(){
 if(closing)return;closing=true;
 server.close();server.closeAllConnections();
 await Promise.all([reader.configure?.shutdown(),reader.preview?.shutdown()]);
 // Keep this owned supervisor alive until compiler completion; never abandon a Build.
 while(reader.build?.isActive())await new Promise(r=>setTimeout(r,100));
 // Let bounded configuration/discovery children and single-shot reaping drain.
 // A forced exit here could orphan an inspection that was already in flight.
 process.exitCode=0;
}
process.stdin.resume();process.stdin.on('end',()=>void close());
process.stdin.on('data',()=>void close());
for(const signal of ['SIGINT','SIGTERM'] as const)process.on(signal,()=>void close());

}
void main().catch(error=>{console.error(error instanceof Error?error.message:'Desktop Host startup failed.');process.exitCode=1;});
