import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {ConfigureRunner} from '../host/configureRunner.ts';
import {BuildRunner} from '../host/buildRunner.ts';
import {ARCH_PROFILE} from '../host/buildProfile.ts';

test('real CMake build failure preserves the last successful binary and manifest, then recovers',async()=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch build 中文 failure-'));
 const configure=new ConfigureRunner({id:'real-build',sourceRoot:root,buildDirRelative:'build space',generator:'Ninja',definitions:{CMAKE_BUILD_TYPE:'Release'}});
 try{
  await mkdir(root+'/studio');
  await writeFile(root+'/case.cpp','int main() { return 0; }\n');
  await writeFile(root+'/CMakeLists.txt','cmake_minimum_required(VERSION 3.20)\nproject(BuildFailure CXX)\nset(CMAKE_RUNTIME_OUTPUT_DIRECTORY "'+root+'/build space/bin")\nadd_executable(ARCH case.cpp)\n');
  const configured=await configure.run('project','real-build');
  assert.equal(configured.state,'succeeded',configured.error);
  const profile={...ARCH_PROFILE,id:'real-build',managedSourceRoot:root,buildDirRelative:'build space',outputBinaryRelative:'build space/bin/ARCH',sourceRelativePath:'case.cpp',trackedInputs:['CMakeLists.txt','case.cpp','build space/CMakeCache.txt'],parallelism:1};
  const build=new BuildRunner(root,'project',profile);
  async function complete(){
   const deadline=Date.now()+15000;
   while(build.isActive()&&Date.now()<deadline)await new Promise(resolve=>setTimeout(resolve,20));
   assert.equal(build.isActive(),false,'real build did not finish within its fixture budget');
   return build.snapshot();
  }
  const first=await build.start('project',profile.id);
  const success=await complete();
  assert.equal(success.state,'succeeded',success.latestResult?.error);
  assert.equal(success.latestResult?.exitCode,0);
  const binary=await readFile(root+'/'+profile.outputBinaryRelative);
  const saved=success.lastSuccessfulBuild;
  assert.equal(saved?.buildId,first.buildId);
  assert.equal(success.binaryState,'freshness-unknown');

  const broken='#error EXPECTED_HOST_BUILD_FAILURE\nint main() { return 0; }\n';
  await writeFile(root+'/case.cpp',broken);
  const second=await build.start('project',profile.id);
  const failure=await complete();
  assert.equal(failure.state,'failed');
  assert.notEqual(failure.latestResult?.exitCode,0);
  assert.equal(failure.latestResult?.buildId,second.buildId);
  assert.deepEqual(failure.lastSuccessfulBuild,saved);
  assert.deepEqual(await readFile(root+'/'+profile.outputBinaryRelative),binary);
  assert.equal(await readFile(root+'/case.cpp','utf8'),broken);
  assert.equal(failure.binaryState,'needs-build');
  assert.ok(build.events(second.buildId).events.some(event=>(event.kind==='stdout'||event.kind==='stderr')&&event.text?.includes('EXPECTED_HOST_BUILD_FAILURE')));

  await writeFile(root+'/case.cpp','int main() { return 0; }\n');
  const third=await build.start('project',profile.id);
  const recovery=await complete();
  assert.equal(recovery.state,'succeeded',recovery.latestResult?.error);
  assert.equal(recovery.lastSuccessfulBuild?.buildId,third.buildId);
  assert.notEqual(recovery.lastSuccessfulBuild?.buildId,first.buildId);
  assert.equal(recovery.binaryState,'freshness-unknown');
 }finally{
  await configure.shutdown();
  await rm(root,{recursive:true,force:true});
 }
});
