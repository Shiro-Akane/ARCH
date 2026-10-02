import {openProject} from '../host/project.ts';
import {createHostServer,listenLocal} from '../host/server.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,rm} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {ConfigureRunner} from '../host/configureRunner.ts';
test('controlled Configure uses literal spaced paths, records evidence and refuses another bound tree',async()=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch configure-'));
 try{
  await writeFile(root+'/CMakeLists.txt','cmake_minimum_required(VERSION 3.20)\nproject(HostConfigure NONE)\n');
  const profile={id:'test',sourceRoot:root,buildDirRelative:'build space',generator:'Ninja' as const,definitions:{CMAKE_BUILD_TYPE:'Release'}};
  const runner=new ConfigureRunner(profile);
  await assert.rejects(runner.run('p','browser-unknown'),/Unknown/);
  const first=await runner.run('p','test');
  assert.equal(first.state,'succeeded',first.error);assert.equal(first.exitCode,0);
  assert.equal(first.evidence?.dependenciesComplete,false);
  assert.ok(runner.events()?.events.some(e=>e.kind==='stdout'));
  assert.equal((await runner.run('p','test')).state,'succeeded');
  await writeFile(root+'/build space/CMakeCache.txt','CMAKE_HOME_DIRECTORY:INTERNAL=/different\n');
  assert.match((await runner.run('p','test')).error!,/refusing to migrate/);
  await mkdir(root+'/occupied');await writeFile(root+'/occupied/keep','untouched');
  const occupied=new ConfigureRunner({...profile,buildDirRelative:'occupied'});
  assert.match((await occupied.run('p','test')).error!,/not empty/);
  await writeFile(root+'/CMakeLists.txt','this_is_invalid(\n');
  const bad=new ConfigureRunner({...profile,buildDirRelative:'bad-build'});
  assert.equal((await bad.run('p','test')).state,'failed');
  assert.equal(bad.isActive(),false);
  // A failed first configure may leave no cache. Its owned files must survive a retry.
  await rm(root+'/bad-build/CMakeCache.txt',{force:true});
  await writeFile(root+'/CMakeLists.txt','cmake_minimum_required(VERSION 3.20)\nproject(Recovered NONE)\n');
  assert.equal((await bad.run('p','test')).state,'succeeded');
 }finally{await rm(root,{recursive:true,force:true});}
});

test('Configure cancellation waits for the owned process group and rejects concurrent starts',async()=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch configure cancel-'));
 try{
  await writeFile(root+'/CMakeLists.txt','cmake_minimum_required(VERSION 3.20)\nproject(Cancel NONE)\nexecute_process(COMMAND /usr/bin/cmake -E sleep 30)\n');
  const runner=new ConfigureRunner({id:'cancel',sourceRoot:root,buildDirRelative:'build',generator:'Ninja',definitions:{}});
  const running=runner.run('p','cancel');
  await assert.rejects(runner.run('p','cancel'),/already active/);
  for(let i=0;i<100&&!runner.processId;i++)await new Promise(r=>setTimeout(r,10));
  const pid=runner.processId;assert.ok(pid);
  runner.cancel();
  const result=await running;
  assert.equal(result.state,'cancelled');assert.equal(result.evidence,undefined);
  assert.equal(runner.isActive(),false);assert.equal(runner.processId,undefined);
  assert.throws(()=>process.kill(pid,0),(e:unknown)=>(e as NodeJS.ErrnoException).code==='ESRCH');
 }finally{await rm(root,{recursive:true,force:true});}
});

test('HTTP Configure rejects browser authority and publishes actual completion',async()=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch configure http-'));
 await writeFile(root+'/CMakeLists.txt','cmake_minimum_required(VERSION 3.20)\nproject(Http NONE)\n');
 const reader=await openProject({project:root}),projectId=reader.snapshot().session.projectId;
 const configure=new ConfigureRunner({id:'http',sourceRoot:root,buildDirRelative:'build',generator:'Ninja',definitions:{}});
 const server=createHostServer({...reader,configure},'http://127.0.0.1:5173');
 await listenLocal(server,0);const address=server.address();assert.ok(address&&typeof address!=='string');
 const url='http://127.0.0.1:'+address.port;
 const headers={Origin:'http://127.0.0.1:5173','X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION,'Content-Type':'application/json'};
 const post=(body:unknown)=>fetch(url+'/api/configure',{method:'POST',headers,body:JSON.stringify(body)});
 try{
  for(const field of ['args','program','env','cwd','shell'])assert.equal((await post({projectId,profileId:'http',[field]:'bad'})).status,400);
  assert.equal((await post({projectId:'stale',profileId:'http'})).status,409);
  assert.equal((await post({projectId,profileId:'unknown'})).status,400);
  assert.equal((await post({projectId,profileId:'http'})).status,202);
  for(let i=0;i<200&&configure.isActive();i++)await new Promise(r=>setTimeout(r,10));
  assert.equal(configure.isActive(),false);
  const response=await fetch(url+'/api/configure/status',{headers});
  assert.equal(response.status,200);const status=await response.json();assert.equal(status.latest.state,'succeeded');
  assert.equal((await fetch(url+'/api/configure/'+status.operationId+'/events',{headers})).status,200);
  assert.equal((await fetch(url+'/api/configure/00000000-0000-0000-0000-000000000000/cancel',{method:'POST',headers})).status,404);
  assert.equal((await fetch(url+'/api/configure/'+status.operationId+'/cancel',{headers})).status,405);
  assert.equal((await fetch(url+'/api/configure/'+status.operationId+'/cancel',{method:'POST',headers,body:'{}'})).status,400);
 }finally{configure.cancel();await new Promise<void>(resolve=>server.close(()=>resolve()));await rm(root,{recursive:true,force:true});}
});
