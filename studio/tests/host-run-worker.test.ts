import {checkpointFilesystemIdentity} from '../host/runCheckpoint.ts';
import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {randomUUID} from 'node:crypto';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {fingerprint} from '../host/files.ts';
import {readConfig} from '../host/config.ts';
import {executeRun} from '../host/runWorker.ts';
import {RunController} from '../host/runController.ts';
import {RunPreparationRunner} from '../host/runPreparation.ts';
import type {RunJob,RunState} from '../host/runWorker.ts';
const worker=fileURLToPath(new URL('../host/runWorker.ts',import.meta.url));
async function setup(script:string){
 const root=await mkdtemp(path.join(os.tmpdir(),'ARCH run 中文 space ')),runId=randomUUID();
 const relative='studio/.local/runs/'+runId,directory=root+'/'+relative;
 await mkdir(directory,{recursive:true});
 await writeFile(root+'/ARCH',script,{mode:0o755});await writeFile(root+'/saved.par','exact raw bytes = 1\n');
 await writeFile(directory+'/input.par','exact raw bytes = 1\n',{mode:0o400});
 const binary=await fingerprint(root,'ARCH','executable');
 const input=await readConfig(root,'saved.par',runId);
 const job:RunJob={version:'1',runId,projectRoot:root,caseId:'Sod',mode:'run',binaryRelativePath:'ARCH',
  binaryFingerprint:{sha256:binary.sha256!,size:binary.size!,modifiedTime:binary.modifiedTime!},
  configRelativePath:'saved.par',configFingerprint:input.fingerprint,inputRelativePath:relative+'/input.par',
  confirmedBinary:'compiled-version',createdAt:new Date().toISOString()};
 await writeFile(directory+'/job.json',JSON.stringify(job));
 return {root,directory,job};
}
async function waitState(directory:string,predicate:(s:RunState)=>boolean){
 const deadline=Date.now()+10000;
 while(Date.now()<deadline){
  const state=await readFile(directory+'/state.json','utf8').then(s=>JSON.parse(s) as RunState).catch(()=>undefined);
  if(state&&predicate(state))return state;
  await new Promise(r=>setTimeout(r,30));
 }
 throw new Error('Run state timed out');
}
test('confirmed exact input runs without shell argument interpretation; status and output persist',async()=>{
 const {root,directory,job}=await setup('#!/bin/sh\nprintf "%s\\n" "$1" "$2"\nexit 0\n');
 try{
  const state=await executeRun(job,directory);
  assert.equal(state.state,'succeeded');assert.equal(state.exitCode,0);assert.ok(state.processId);assert.ok(state.processIdentity);
  assert.equal(await readFile(directory+'/console.log','utf8'),'Sod\n'+directory+'/input.par\n');
  await assert.rejects(executeRun(job,directory),/EEXIST/);
 }finally{await rm(root,{recursive:true,force:true});}
});
test('changed config or binary is rejected before any executable starts',async()=>{
 for(const changed of ['saved.par','ARCH']){
  const {root,directory,job}=await setup('#!/bin/sh\nexit 0\n');
  try{
   await writeFile(root+'/'+changed,'changed after confirmation');
   const state=await executeRun(job,directory);
   assert.equal(state.state,'failed');assert.match(state.error!,/changed after confirmation/i);
   assert.equal(state.processId,undefined);
  }finally{await rm(root,{recursive:true,force:true});}
 }
});
test('run survives launcher exit and only matching run ID requests Stop',async()=>{
 const {root,directory,job}=await setup('#!/bin/sh\nexec /usr/bin/sleep 30\n');
 let pid:number|undefined;
 try{
  const parent=path.join(directory,'parent.mjs');
  await writeFile(parent,"import {spawn} from 'node:child_process';const p=spawn(process.execPath,"+JSON.stringify([worker,directory])+",{detached:true,stdio:'ignore'});p.unref();");
  await promisify(execFile)(process.execPath,[parent],{timeout:3000});
  const running=await waitState(directory,s=>s.state==='running');pid=running.processId;
  assert.ok(pid);assert.equal(running.processIdentity,'captured');assert.ok(running.processStartTicks);process.kill(pid!,0);
  await writeFile(directory+'/stop-request','wrong-run-id');
  await new Promise(r=>setTimeout(r,250));process.kill(pid!,0);
  // A newly connected Host has no in-memory knowledge of the delivered job.
  const projectId=randomUUID();
  const recovered=new RunController(new RunPreparationRunner(root,projectId,'ARCH',()=>readConfig(root,'saved.par',projectId)));
  const history=await recovered.history();
  assert.equal(history.length,1);assert.equal(history[0].runId,job.runId);
  assert.equal(history[0].configSha,job.configFingerprint.sha256);
  assert.equal(history[0].state?.state,'running');assert.equal(history[0].diagnostic,null);
  // Remove only this test's deliberately invalid request before the public Stop.
  await rm(directory+'/stop-request');
  assert.deepEqual(await recovered.stop(job.runId),{runId:job.runId,stopRequested:true});
  const ended=await waitState(directory,s=>!!s.finishedAt);
  assert.equal(ended.state,'stopped');assert.equal(ended.signal,'SIGTERM');
  assert.equal((await recovered.status(job.runId)).state,'stopped');
  assert.equal((await recovered.history())[0].state?.state,'stopped');
  assert.throws(()=>process.kill(pid!,0));
 }finally{
  if(pid)try{process.kill(-pid,'SIGKILL');}catch{/* already reaped */}
  await rm(root,{recursive:true,force:true});
 }
});
test('an unrelated process is untouched by owned Stop',async()=>{
 const other=spawn('/usr/bin/sleep',['30'],{stdio:'ignore'});
 const {root,directory,job}=await setup('#!/bin/sh\nexec /usr/bin/sleep 30\n');
 let pid:number|undefined;
 try{
  const p=spawn(process.execPath,[worker,directory],{stdio:'ignore'});
  const closed=new Promise<void>(resolve=>p.once('close',()=>resolve()));
  pid=(await waitState(directory,s=>s.state==='running')).processId;
  await writeFile(directory+'/stop-request',job.runId);
  await closed;
  process.kill(other.pid!,0);
 }finally{
  other.kill();if(pid)try{process.kill(-pid,'SIGKILL');}catch{/* exited */}
  await rm(root,{recursive:true,force:true});
 }
});

test('Stop also terminates a TERM-ignoring child after its group leader exits',async()=>{
 const {root,directory,job}=await setup('#!/bin/sh\n/bin/sh -c \'trap "" TERM; exec /usr/bin/sleep 30\' &\necho $! > "$0.child"\nwait\n');
 let descendant:number|undefined;
 try{
  const p=spawn(process.execPath,[worker,directory],{stdio:'ignore'});
  const closed=new Promise<void>(resolve=>p.once('close',()=>resolve()));
  await waitState(directory,s=>s.state==='running');
  await new Promise(r=>setTimeout(r,100));
  descendant=Number(await readFile(root+'/ARCH.child','utf8'));
  await writeFile(directory+'/stop-request',job.runId);
  await closed;
  const stat=await readFile('/proc/'+descendant+'/stat','utf8').catch(()=>undefined);
  assert.ok(!stat||stat.slice(stat.lastIndexOf(')')+2).startsWith('Z '),'owned descendant must be exited');
 }finally{if(descendant)try{process.kill(descendant,'SIGKILL');}catch{/* gone */}await rm(root,{recursive:true,force:true});}
});

test('a Stop request recorded before terminal handoff prevents any Core start',async()=>{
 const {root,directory,job}=await setup('#!/bin/sh\nexit 0\n');
 try{
  await writeFile(directory+'/stop-request',job.runId);
  const state=await executeRun(job,directory);
  assert.equal(state.state,'stopped');assert.equal(state.processId,undefined);assert.ok(state.finishedAt);
 }finally{await rm(root,{recursive:true,force:true});}
});

test('Restart worker rejects missing or changed checkpoint handoff before Core starts',async()=>{
 for(const mutation of ['missing-identity','changed','unchanged']){
  const {root,directory,job}=await setup('#!/bin/sh\nexit 0\n');
  try{
   job.mode='restart';await writeFile(root+'/checkpoint.h5','checkpoint-one');
   if(mutation!=='missing-identity')job.checkpoint={path:root+'/checkpoint.h5',filesystemIdentity:await checkpointFilesystemIdentity(root+'/checkpoint.h5')};
   if(mutation==='missing-identity'){await assert.rejects(executeRun(job,directory),/handoff identity/);continue;}
   if(mutation==='changed'){await rm(root+'/checkpoint.h5');await writeFile(root+'/checkpoint.h5','checkpoint-two');}
   const state=await executeRun(job,directory);
   if(mutation==='changed'){assert.equal(state.state,'failed');assert.match(state.error!,/checkpoint changed/);assert.equal(state.processId,undefined);}
   else{assert.equal(state.state,'succeeded');assert.ok(state.processId);}
  }finally{await rm(root,{recursive:true,force:true});}
 }
});
