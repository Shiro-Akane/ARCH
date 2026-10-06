import {saveManifest,loadManifest} from '../host/buildManifest.ts';
import {BuildRunner} from '../host/buildRunner.ts';
import {profileFingerprint} from '../host/buildProfile.ts';
import type {BuildProfile,BuildManifest} from '../src/host/contracts.ts';
import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,writeFile,rm,symlink,mkdir,chmod} from 'node:fs/promises';
import os from 'node:os';
import {readToolRuntimeEvidence,runtimeFingerprint,parseRuntimeElf,parseRuntimeCache,changedRuntimeInputs,sameRuntimeInputs,validRuntimeEvidence} from '../host/toolRuntimeEvidence.ts';
const header='Class: ELF64\nMachine: Advanced Micro Devices X86-64\n';
test('runtime declarations reject architecture/unsafe dependencies and retain unmodeled search paths',()=>{
 assert.deepEqual(parseRuntimeElf(header+'(NEEDED) Shared library: [libm.so.6]\n(RUNPATH) Library runpath: [$ORIGIN/lib]\n').searchPaths,['$ORIGIN/lib']);
 for(const text of ['Class: ELF32\n',header+'(NEEDED) Shared library: [/unsafe/x]\n',header+'(NEEDED) Shared library: [x]\n(NEEDED) Shared library: [x]\n'])assert.throws(()=>parseRuntimeElf(text));
 assert.equal(parseRuntimeCache('x (libc6,x86-64) => /a\nx (libc6,x86-64) => /b\nx (libc6) => /i386\n').get('x')?.size,2);
});
test('runtime graph retains missing/RPATH uncertainty, detects content drift and never runs root payload',async()=>{
 const dir=await mkdtemp(os.tmpdir()+'/arch tool runtime-');
 try{
  const tool=dir+'/tool',library=dir+'/libx';await writeFile(tool,'#!/bin/sh\ntouch '+dir+'/executed\n');await writeFile(library,'lib1');
  let rpath=false;
  const probe=async(program:string,args:string[])=>program.endsWith('ldconfig')?'libx.so (libc6,x86-64) => '+library+'\n':header+(args.at(-1)===tool?'(NEEDED) Shared library: [libx.so]\n'+(rpath?'(RUNPATH) Library runpath: [$ORIGIN]\n':''):'');
  const before=await readToolRuntimeEvidence([tool],probe);
  assert.equal(before.dependenciesComplete,false);assert.equal(before.nodes.length,2);assert.equal(before.unresolved.length,0);
  assert.equal(validRuntimeEvidence(before),true);assert.equal(sameRuntimeInputs(before,structuredClone(before)),true);assert.equal(sameRuntimeInputs(undefined,before),false);
  await writeFile(library,'lib2');
  const after=await readToolRuntimeEvidence([tool],probe);
  assert.ok(changedRuntimeInputs(before,after).includes(library));
  rpath=true;const limited=await readToolRuntimeEvidence([tool],probe);assert.equal(limited.unresolved[0].reason,'unmodeled-rpath');
  rpath=false;const missing=await readToolRuntimeEvidence([tool],async(program,args)=>program.endsWith('ldconfig')?'':probe(program,args));
  assert.equal(missing.unresolved[0].reason,'missing-or-ambiguous-cache-candidate');
  assert.equal((await runtimeFingerprint(tool)).path,tool);
  await assert.rejects(runtimeFingerprint(dir));await assert.rejects(runtimeFingerprint('relative'));
  await assert.rejects(runtimeFingerprint(dir+'/executed'));
  await symlink(tool,dir+'/alias');assert.equal((await runtimeFingerprint(dir+'/alias')).resolvedPath,tool);
  const bad=structuredClone(before);bad.nodes[0].sha256='bad';assert.equal(validRuntimeEvidence(bad),false);
  const duplicate=structuredClone(before);duplicate.nodes.push(duplicate.nodes[0]);assert.equal(validRuntimeEvidence(duplicate),false);
  const forged=structuredClone(before);forged.dependenciesComplete=true as never;assert.equal(validRuntimeEvidence(forged),false);
  const excessive=structuredClone(before);excessive.roots=Array(33).fill(tool);assert.equal(validRuntimeEvidence(excessive),false);
 }finally{await rm(dir,{recursive:true,force:true});}
});
test('actual CMake/Ninja runtime graph records candidate libraries while keeping completeness false',async()=>{
 const evidence=await readToolRuntimeEvidence(['/usr/bin/cmake','/usr/bin/ninja']);
 assert.ok(evidence.nodes.length>2);assert.ok(evidence.edges.some(e=>e.kind==='DT_NEEDED'));
 assert.equal(validRuntimeEvidence(evidence),true);assert.equal(evidence.dependenciesComplete,false);
 await assert.rejects(readToolRuntimeEvidence([]));await assert.rejects(readToolRuntimeEvidence(['relative']));
});

test('runtime disk evidence reloads; malformed records reject; legacy stays unknown and known library drift needs build',async()=>{
 const root=await mkdtemp(os.tmpdir()+'/arch runtime disk-');
 try{
  await mkdir(root+'/studio');const library=root+'/lib';await writeFile(library,'candidate1');
  await writeFile(root+'/ARCH','#!/bin/sh\nexit 0\n');await chmod(root+'/ARCH',0o700);
  const runtime=await readToolRuntimeEvidence([library],async(program)=>program.endsWith('ldconfig')?'':header);
  const profile:BuildProfile={id:'runtime-test',displayName:'test',managedSourceRoot:root,buildDirRelative:'build',target:'ARCH',outputBinaryRelative:'ARCH',parallelism:1,trackedInputs:[],dependenciesComplete:false,compilerDependencyMode:'ninja'};
  const manifest:BuildManifest={manifestVersion:'1',buildId:'runtime-build',projectId:'p',profileId:profile.id,managedSourceRoot:root,
   buildProfileFingerprint:profileFingerprint(profile),trackedInputFingerprints:[],preBuildInputFingerprints:[],inputsStableDuringBuild:true,
   buildDirectory:root+'/build',target:'ARCH',startedAt:'2026-10-04T00:00:00Z',finishedAt:'2026-10-04T00:00:01Z',
   outputBinary:{relativePath:'ARCH',absolutePath:root+'/ARCH',fingerprint:{sha256:'0'.repeat(64),size:1,modifiedTime:'2026-10-04T00:00:01Z'}},
   toolRuntime:runtime,toolRuntimeStableDuringBuild:true};
  await saveManifest(profile,manifest);assert.deepEqual((await loadManifest(profile))?.toolRuntime,runtime);
  const corrupt=structuredClone(manifest);corrupt.toolRuntime!.nodes[0].sha256='bad';
  await saveManifest(profile,corrupt);assert.equal(await loadManifest(profile),undefined);
  const duplicate=structuredClone(manifest);duplicate.toolRuntime!.nodes.push(duplicate.toolRuntime!.nodes[0]);
  await saveManifest(profile,duplicate);assert.equal(await loadManifest(profile),undefined);
  const legacy=structuredClone(manifest);delete legacy.toolRuntime;delete legacy.toolRuntimeStableDuringBuild;
  await saveManifest(profile,legacy);assert.ok(await loadManifest(profile));
  assert.equal((await new BuildRunner(root,'p',profile).initialize()).binaryState,'freshness-unknown');
  await saveManifest(profile,manifest);await writeFile(library,'candidate2');
  const changed=await new BuildRunner(root,'p',profile).initialize();
  assert.equal(changed.binaryState,'needs-build');assert.ok(changed.changedInputs.includes(library));
 }finally{await rm(root,{recursive:true,force:true});}
});
