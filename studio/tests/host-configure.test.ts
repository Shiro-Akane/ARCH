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
 }finally{await rm(root,{recursive:true,force:true});}
});
