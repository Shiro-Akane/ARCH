/** Packaged desktop entrypoint. Only Electron main supplies this launch envelope. */
import {registeredSourceCase} from './desktopSource.ts';
import {openProject} from './project.ts';
import {BUILD_PROFILES} from './buildProfile.ts';
import {createHostServer,listenLocal} from './server.ts';
import {realpath,stat} from 'node:fs/promises';
import path from 'node:path';
async function main(){
const launch=JSON.parse(Buffer.from(process.argv[2]??'', 'base64url').toString('utf8')) as {project?:string;cwd:string;source?:string;config?:string;binary?:string;caseId?:string;origin:string;token:string};
if(!/^[a-f0-9]{64}$/.test(launch.token))throw new Error('Invalid desktop ownership token.');
async function discover(start:string){
 let current=await realpath(start);if(!(await stat(current)).isDirectory())current=path.dirname(current);
 for(;;){if(await stat(path.join(current,'CMakeLists.txt')).then(s=>s.isFile(),()=>false)&&await stat(path.join(current,'simulation')).then(s=>s.isDirectory(),()=>false))return current;const parent=path.dirname(current);if(parent===current)throw new Error('ARCH project not found. Choose a project containing CMakeLists.txt and simulation/.');current=parent;}
}
const root=await discover(launch.project??launch.source??launch.cwd);
function relative(input:string){const absolute=path.resolve(launch.cwd,input);const rel=path.relative(root,absolute);if(rel.startsWith('../')||path.isAbsolute(rel))throw new Error('Selected file is outside the managed project.');return rel;}
const binary=launch.binary?relative(launch.binary):undefined;
const profile=BUILD_PROFILES.find(p=>p.managedSourceRoot===root&&(!binary||p.outputBinaryRelative===binary));
if(binary&&!await stat(path.join(root,binary)).then(s=>s.isFile(),()=>false))throw new Error('Selected binary is missing. Select an existing ARCH executable; no automatic Build was started.');
if(!profile)throw new Error('No approved Host-owned Build Profile matches this project/binary. Register a trusted profile; build trees are never rebound automatically.');
const config=launch.config?relative(launch.config):'simulation/Sod/Sod.par';
const reader=await openProject({project:root,config,buildProfile:profile.id});
const build=reader.build?.snapshot();
if(!build?.configured)throw new Error(build?.reason??'Configured build directory unavailable.');
if(!reader.snapshot().session.executable?.exists)throw new Error('ARCH executable is missing. Restore/build the approved binary outside this launch, then retry.');
const registry=await reader.workflow?.discovery();
let caseId=launch.caseId??'Sod';
if(launch.source)caseId=registeredSourceCase(registry?.cases??[],root,relative(launch.source),launch.caseId);
if(!registry?.cases.some(c=>c.caseId===caseId))throw new Error('Selected case is not registered by this binary.');
const server=createHostServer(reader,launch.origin,{token:launch.token});
await listenLocal(server,0);
const port=(server.address() as {port:number}).port;
console.log(JSON.stringify({kind:'desktop-ready',port,pid:process.pid,project:root,caseId,config,token:launch.token}));
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
