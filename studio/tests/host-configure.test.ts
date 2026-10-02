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
