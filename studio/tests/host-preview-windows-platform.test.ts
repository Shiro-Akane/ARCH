import test from 'node:test';
import assert from 'node:assert/strict';
import {sessionCapability,PreviewSession} from '../host/previewSession.ts';
import {captureOwned,spawnOwned,requireWindowsProcess,windowsProcessIdentity} from '../host/hostProcess.ts';
import {readFileSync} from 'node:fs';
import {mkdtemp,writeFile,readFile,rm} from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import type {spawn,SpawnOptions} from 'node:child_process';
import {validateRegistry} from '../src/host/workflowValidation.ts';

function capability(platform='linux'){
 return {extensions:{session:{supported:true,version:'1',workerPlatform:platform,transport:'ndjson',maxInFlight:1,
  maxRequests:256,maxRequestBytes:8*1024*1024,maxResponseBytes:9*1024*1024,maxConfigBytes:1024*1024,
  heavyRequestCpuSeconds:300,heavyRequestWallSeconds:360,meshRequestCpuSeconds:30,meshRequestWallSeconds:45,
  ...(platform==='windows'?{resourcePolicy:{kind:'windows-job-commit-user-v1',memoryKind:'committed',
   processCommitMiB:1024,cpuClock:'user',cpuScope:'resource-job',budgetRenewal:'per-request'}}:{processAddressSpaceMiB:1024})}}};
}

test('Preview policy admits explicit Windows commit/user semantics and keeps Linux metadata compatible',()=>{
 const linux=sessionCapability(capability());assert.ok(linux);assert.equal(linux.workerPlatform,undefined);
 const windows=sessionCapability(capability('windows'));assert.equal(windows?.workerPlatform,'windows');
 assert.equal(windows?.resourcePolicy,'windows-job-commit-user-v1');
 assert.equal(sessionCapability({extensions:{session:{supported:false}}}),undefined);
});
test('Preview policy rejects ambiguous or weaker Windows resource claims',()=>{
 const changes:Record<string,unknown>[]=[{kind:'unknown'},{memoryKind:'address-space'},
  {processCommitMiB:2048},{cpuClock:'user+kernel'},{cpuScope:'process'},
  {budgetRenewal:'lifetime-only'},{extra:true}];
 for(const change of changes){const c=capability('windows');Object.assign(c.extensions.session.resourcePolicy!,change);assert.throws(()=>sessionCapability(c));}
 for(const change of [{heavyRequestCpuSeconds:301},{meshRequestCpuSeconds:31},{processAddressSpaceMiB:1024}]){
  const c=capability('windows');Object.assign(c.extensions.session,change);assert.throws(()=>sessionCapability(c));
 }
 const c=capability('windows');delete c.extensions.session.resourcePolicy;assert.throws(()=>sessionCapability(c));
 assert.throws(()=>sessionCapability({extensions:{session:{...capability().extensions.session,workerPlatform:{toString:()=> 'linux'}}}}));
});

test('registry preserves Linux metadata and requires the complete Windows inspection policy',()=>{
 const legacy=JSON.parse(readFileSync(new URL('../../src/api/examples/local-workflow/registered-cases.json',import.meta.url),'utf8'));
 assert.ok(validateRegistry(legacy).length>0);
 const windows=structuredClone(legacy);
 for(const entry of windows.cases)Object.assign(entry.inspection,{workerPlatform:'windows',processCpuSeconds:300,
  resourcePolicy:capability('windows').extensions.session.resourcePolicy});
 assert.equal(validateRegistry(windows).length,legacy.cases.length);
 for(const change of [{workerPlatform:'unknown'},{resourcePolicy:undefined},{processCpuSeconds:301},
  {processAddressSpaceMiB:1024},{compiledSourceSha256:'invalid'},
  {resourcePolicy:{...capability('windows').extensions.session.resourcePolicy,cpuScope:'process'}}]){
  const invalid=structuredClone(windows);Object.assign(invalid.cases[0].inspection,change);
  assert.throws(()=>validateRegistry(invalid));
 }
});

const native=process.platform==='win32';
function tools(){
 assert.ok(process.env.ARCH_TEST_SUPERVISOR);
 return {supervisor:process.env.ARCH_TEST_SUPERVISOR!,path:process.env.PATH!};
}
if(process.platform==='win32') test('native capture retains tighter byte/time limits and waits for owned cancellation',{skip:!native},async()=>{
 const context=tools();
 assert.equal((await captureOwned(process.execPath,['-e',"process.stdout.write('ok')"],'',process.cwd(),context,{timeoutMs:3000,stdoutBytes:64})).stdout,'ok');
 await assert.rejects(captureOwned(process.execPath,['-e',"process.stdout.write('x'.repeat(2048));setInterval(()=>{},1000)"],'',process.cwd(),context,{stdoutBytes:1024}),/byte limit/);
 await assert.rejects(captureOwned(process.execPath,['-e','setInterval(()=>{},1000)'],'',process.cwd(),context,{timeoutMs:100}),/wall-clock limit/);
 const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),100);
 try{await assert.rejects(captureOwned(process.execPath,['-e','setInterval(()=>{},1000)'],'',process.cwd(),context,{signal:controller.signal}),/cancelled/);}
 finally{clearTimeout(timer);}
 await assert.rejects(captureOwned(process.execPath,[],'',process.cwd(),context,{stdoutBytes:8*1024*1024+1}),/only tighten/);
});
if(process.platform==='win32') test('native session refuses the Linux or absent resource policy before launch',{skip:!native},()=>{
 assert.throws(()=>new PreviewSession({binary:process.execPath,cwd:process.cwd(),capability:sessionCapability(capability())!}),/approved resource contract/);
});

if(process.platform==='win32') test('native session cancellation and timeout reclaim the contained worker and descendant',{skip:!native,timeout:20000},async()=>{
 // The fixture isolates transport ownership; real Core resource enforcement has
 // its own native harness and is not inferred from this deliberately idle worker.
 const context=tools(),directory=await mkdtemp(path.join(os.tmpdir(),'arch session 中文 '));
 const script=path.join(directory,'idle.cjs'),trace=path.join(directory,'identity.json');
 await writeFile(script,`const fs=require('node:fs'),cp=require('node:child_process');
const child=cp.spawn(process.execPath,['-e','setInterval(()=>{},1000)'],{stdio:'ignore'});
fs.writeFileSync(process.argv[2],JSON.stringify([process.pid,child.pid]));
console.log(JSON.stringify({kind:'preview-session-ready',version:'1',sequence:0,capability:${JSON.stringify(capability('windows').extensions.session)}}));
require('node:readline').createInterface({input:process.stdin}).on('line',()=>{});
`);
 try{
  for(const action of ['cancel','timeout']){
   const launch=((_binary:string,_args:string[],options:SpawnOptions)=>
    spawnOwned(process.execPath,[script,trace],options,context)) as typeof spawn;
   const session=new PreviewSession({binary:process.execPath,cwd:directory,windowsTools:context,
    capability:sessionCapability(capability('windows'))!,spawn:launch,timeoutMs:action==='timeout'?100:3000});
   try{
    await session.ready;
    const identities=(JSON.parse(await readFile(trace,'utf8')) as number[]).map(pid=>windowsProcessIdentity(context,pid));
    assert.equal(identities.length,2);assert.ok(identities.every(identity=>identity.alive));
    const pending=session.request({command:'--preview',caseId:'Sod',requestId:action,configText:'x_pos=.5'});
    const rejected=assert.rejects(pending,action==='timeout'?/timed out/:/cancelled/);
    if(action==='cancel'){
     // Let request() resume from ready and install its in-flight state first.
     await new Promise(resolve=>setImmediate(resolve));await session.terminate();
    }
    await rejected;await session.closed;
    for(const identity of identities){
     const deadline=Date.now()+3000;
     while(Date.now()<deadline){try{requireWindowsProcess(context,identity);}catch{break;}await new Promise(resolve=>setTimeout(resolve,10));}
     assert.throws(()=>requireWindowsProcess(context,identity),/changed|no longer alive|Open process identity \(Win32 87\)/);
    }
   }finally{await session.terminate();}
  }
 }finally{await rm(directory,{recursive:true,force:true});}
});
