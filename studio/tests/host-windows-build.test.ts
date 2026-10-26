import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import os from 'node:os';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {ConfigureRunner} from '../host/configureRunner.ts';
import {BuildRunner} from '../host/buildRunner.ts';
import {openProject} from '../host/project.ts';
import {loadManifest,saveManifest} from '../host/buildManifest.ts';
import {fingerprintLinkDependencies} from '../host/linkDependencies.ts';
import {readBuildToolchainEvidence} from '../host/cmakeEvidence.ts';
import {readBuildGeneratorEvidence} from '../host/cmakeEvidence.ts';
import {validRuntimeEvidence} from '../host/toolRuntimeEvidence.ts';
import type {BuildProfile} from '../src/host/contracts.ts';

test('native Configure/Build persists real evidence, reopens, invalidates changes and preserves the old manifest on failure', {skip:process.platform!=='win32',timeout:360000},async()=>{
 const tools={cmake:process.env.ARCH_TEST_CMAKE!,supervisor:process.env.ARCH_TEST_SUPERVISOR!,path:process.env.PATH!,ninja:process.env.ARCH_TEST_NINJA!,objdump:process.env.ARCH_TEST_OBJDUMP!,publisher:process.env.ARCH_TEST_PUBLISHER!,rc:process.env.ARCH_TEST_RC!};
 for(const key of ['cmake','supervisor','ninja','objdump','publisher','rc'] as const)assert.ok(tools[key],key);
 const root=await mkdtemp(path.join(os.tmpdir(),'arch native build '));
 const build=path.join(root,'build');
 const p:BuildProfile={id:'native-evidence-fixture',displayName:'Native evidence fixture',managedSourceRoot:root,buildDirRelative:'build',outputBinaryRelative:'build/bin/ARCH.exe',target:'ARCH',parallelism:2,trackedInputs:['CMakeLists.txt','main.cpp','build/CMakeCache.txt'],compilerDependencyMode:'ninja',linkDependencyFile:'ARCH.link.d',retainGnuLtoInputs:true,dependenciesComplete:false};
 const configure=new ConfigureRunner({id:p.id,sourceRoot:root,buildDirRelative:'build',generator:'Ninja',windowsTools:tools,definitions:{CMAKE_BUILD_TYPE:'Release',CMAKE_CXX_COMPILER:process.env.ARCH_TEST_CXX!,CMAKE_MAKE_PROGRAM:tools.ninja,CMAKE_RC_COMPILER:tools.rc}});
 async function finish(runner:BuildRunner){const deadline=Date.now()+120000;while(runner.isActive()&&Date.now()<deadline)await new Promise(resolve=>setTimeout(resolve,25));assert.equal(runner.isActive(),false,'Native fixture completion deadline exceeded');return runner.snapshot();}
 try{
  await mkdir(path.join(root,'studio'));
  await writeFile(path.join(root,'main.cpp'),'#include <cstdio>\nint main(){std::puts("native");return 0;}\n');
  await writeFile(path.join(root,'CMakeLists.txt'),'cmake_minimum_required(VERSION 3.20)\nproject(NativeEvidence CXX)\nadd_executable(ARCH main.cpp)\nset_target_properties(ARCH PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")\ntarget_compile_options(ARCH PRIVATE -flto)\ntarget_link_options(ARCH PRIVATE -flto -Wl,-plugin-opt=-debug -Wl,--dependency-file=ARCH.link.d)\n');
  const configured=await configure.run('p',p.id);assert.equal(configured.state,'succeeded',configured.error);
  await assert.rejects(readBuildGeneratorEvidence(root,build,tools.cmake,tools.cmake),/Ninja differs/);
  await assert.rejects(readBuildToolchainEvidence(build,{...tools,rc:tools.cmake}),/resource compiler/);
  const reader=await openProject({project:root,buildProfile:p.id,trustedBuildProfile:p,configureProfile:configure.profile});
  const runner=reader.build!;const projectId=reader.snapshot().session.projectId;assert.ok(runner);assert.equal(runner.snapshot().configured,true);
  for(let i=0;i<2;i++){
   await runner.start(projectId,p.id);const state=await finish(runner);assert.equal(state.state,'succeeded',state.latestResult?.error+' '+JSON.stringify(runner.events(state.latestResult!.buildId)));
   const m=await loadManifest(p);assert.ok(m);assert.equal(m.buildId,state.latestResult?.buildId);
   assert.equal(m.configurationInputError,undefined);assert.equal(m.compilerInputError,undefined);assert.equal(m.compilerDriverError,undefined);assert.equal(m.linkInputError,undefined);assert.equal(m.toolRuntimeError,undefined);
   assert.equal(m.configurationInputsStableDuringBuild,true);assert.equal(m.compilerDriversStableDuringBuild,true);assert.equal(m.toolRuntimeStableDuringBuild,true);
   assert.equal(validRuntimeEvidence(m.toolRuntime),true);assert.ok(m.compilerInputs!.files.length>0);assert.equal(m.linkInputs!.unavailable.length,0);
   assert.equal(state.binaryState,'freshness-unknown');
   if(i===1)assert.equal(m.compilerInputsStableDuringBuild,true);
  }
  const saved=(await loadManifest(p))!;
  const reopened=new BuildRunner(root,'p2',p,{windowsTools:tools});assert.equal((await reopened.initialize()).lastSuccessfulBuild?.buildId,saved.buildId);
  // Remove a reported input owned by this disposable fixture. Missing
  // recorded link objects must remain visible in the evidence and invalidate freshness.
  const retained=saved.linkInputs!.files.find(f=>f.path.startsWith(build+path.sep)&&/\.obj$/.test(f.path));
  assert.ok(retained,'Expected actual recorded fixture link input');
  const retainedBytes=await readFile(retained.path);
  await rm(retained.path);
  const missing=await fingerprintLinkDependencies(build,'ARCH.link.d',path.join(build,'bin/ARCH.exe'));
  assert.ok(missing.unavailable.some(f=>f.path===retained.path));
  assert.equal((await reopened.refreshFreshness()).binaryState,'needs-build');
  await writeFile(retained.path,retainedBytes);
  await writeFile(path.join(root,'main.cpp'),'#error deliberately fail native build\n');
  const changed=await reopened.refreshFreshness();assert.equal(changed.binaryState,'needs-build');assert.ok(changed.changedInputs.includes('main.cpp'));
  await reopened.start('p2',p.id);assert.equal((await finish(reopened)).state,'failed');assert.equal((await loadManifest(p))?.buildId,saved.buildId);
  const corrupted=structuredClone(saved);corrupted.toolRuntime!.nodes[0].sha256='bad';await saveManifest(p,corrupted,tools);assert.equal(await loadManifest(p),undefined);
  await saveManifest(p,saved,tools);assert.equal((await loadManifest(p))?.buildId,saved.buildId);
  const linker=await readFile(path.join(build,'ARCH.link.d'),'utf8');assert.match(linker,/[A-Za-z]:[\\/]/);
 }finally{await configure.shutdown();await rm(root,{recursive:true,force:true});}
});
