import {saveManifest,loadManifest} from '../host/buildManifest.ts';
import {execFile} from 'node:child_process';import {promisify} from 'node:util';
import {fixture,fakeSpawn,finished} from './build-fixture.ts';
import {readFile} from 'node:fs/promises';
import {writeFileSync} from 'node:fs';
import test from 'node:test';import assert from 'node:assert/strict';import {mkdir,writeFile,rm} from 'node:fs/promises';
import {validateProfile} from '../host/buildProfile.ts';import {BuildRunner} from '../host/buildRunner.ts';
test('profile validates bindings and refuses traversal, wrong source, missing CMake',async()=>{const {root,p}=await fixture();try{await validateProfile(root,p);for(const invalid of [{...p,managedSourceRoot:'/other'},{...p,buildDirRelative:'../escape'},{...p,target:'--help'},{...p,trackedInputs:['case.par']}])await assert.rejects(validateProfile(root,invalid));await assert.rejects(validateProfile(root,p,'/no/cmake'));}finally{await rm(root,{recursive:true,force:true});}});
test('unknown profile and missing configured directory never start a process',async()=>{const {root,p}=await fixture();try{const runner=new BuildRunner(root,'project',{...p,buildDirRelative:'missing'});await assert.rejects(runner.start('project','unknown'));await assert.rejects(runner.start('project',p.id));assert.equal(runner.snapshot().state,'not-configured');}finally{await rm(root,{recursive:true,force:true});}});

test('successful process creates manifest; exit zero without binary fails; previous success survives failed attempt',async()=>{const {root,p}=await fixture();try{await mkdir(root+'/studio');const runner=new BuildRunner(root,'p',p,{spawn:fakeSpawn(root)});const started=await runner.start('p',p.id);await assert.rejects(runner.start('p',p.id),/busy/);const s=await finished(runner);assert.equal(s.state,'succeeded');assert.equal(s.lastSuccessfulBuild?.buildId,started.buildId);assert.equal(s.lastSuccessfulBuild?.outputBinary.fingerprint.size,20);assert.equal(s.lastSuccessfulBuild?.managedSourceRoot,root);assert.ok(runner.events(started.buildId).events.some(x=>x.kind==='stdout'));const failed=new BuildRunner(root,'p',p,{spawn:fakeSpawn(root,2,false)});await failed.initialize();await failed.start('p',p.id);assert.equal((await finished(failed)).state,'failed');assert.equal(failed.snapshot().lastSuccessfulBuild?.buildId,started.buildId);await rm(root+'/build/bin/ARCH');const missing=new BuildRunner(root,'p',p,{spawn:fakeSpawn(root,0,false)});await missing.start('p',p.id);assert.equal((await finished(missing)).state,'failed');assert.match(missing.snapshot().latestResult!.error!,/executable/);assert.equal(await readFile(root+'/case.cpp','utf8'),'// fixture');}finally{await rm(root,{recursive:true,force:true});}});

test('freshness tracks explicit inputs, not runtime config/repository dirt; profile change becomes unknown',async()=>{const {root,p}=await fixture();try{await mkdir(root+'/studio');const runner=new BuildRunner(root,'p',p,{spawn:fakeSpawn(root)});await runner.start('p',p.id);await finished(runner);assert.equal(runner.snapshot().binaryState,'freshness-unknown');await writeFile(root+'/runtime.par','x = 3');await writeFile(root+'/studio/unrelated.txt','dirty');assert.equal((await runner.refreshFreshness()).binaryState,'freshness-unknown');assert.deepEqual(runner.snapshot().changedInputs,[]);await writeFile(root+'/case.cpp','// changed');assert.equal((await runner.refreshFreshness()).binaryState,'needs-build');assert.deepEqual(runner.snapshot().changedInputs,['case.cpp']);const changed=new BuildRunner(root,'p',{...p,parallelism:2});assert.equal((await changed.initialize()).binaryState,'freshness-unknown');assert.match(changed.snapshot().freshnessReason,/configuration changed/);}finally{await rm(root,{recursive:true,force:true});}});

test('inputs changing while a build runs never receive a current-input claim',async()=>{const {root,p}=await fixture();await mkdir(root+'/studio');try{const runner=new BuildRunner(root,'p',p,{spawn:fakeSpawn(root,0,true,100)});await runner.start('p',p.id);await new Promise(r=>setTimeout(r,50));await writeFile(root+'/case.cpp','// changed during compilation');const s=await finished(runner);assert.equal(s.state,'succeeded');assert.equal(s.lastSuccessfulBuild?.inputsStableDuringBuild,false);assert.equal(s.binaryState,'needs-build');}finally{await rm(root,{recursive:true,force:true});}});

test('runner command and environment come only from the Host profile',async()=>{const {root,p}=await fixture();await mkdir(root+'/studio');try{const runFake=fakeSpawn(root) as (...args:unknown[])=>unknown;const runner=new BuildRunner(root,'p',p,{spawn:((program:unknown,args:unknown,options:unknown)=>{assert.equal(program,'/usr/bin/cmake');assert.deepEqual(args,['--build',root+'/build','--target','ARCH','--parallel','4']);const o=options as {cwd:string;shell:boolean;env:Record<string,string>};assert.equal(o.cwd,root);assert.equal(o.shell,false);assert.deepEqual(Object.keys(o.env).sort(),['HOME','LANG','PATH']);return runFake(program,args,options);}) as never});await runner.start('p',p.id);assert.equal((await finished(runner)).state,'succeeded');}finally{await rm(root,{recursive:true,force:true});}});

test('missing migrated tracked input invalidates the profile before any process starts',async()=>{
 const {root,p}=await fixture();
 try{
  const profile={...p,trackedInputs:[...p.trackedInputs,'removed/core/header.h']};
  await assert.rejects(validateProfile(root,profile),/profile migration required: removed\/core\/header\.h/);
  let spawned=false;
  const runner=new BuildRunner(root,'project',profile,{spawn:(()=>{spawned=true;throw new Error('must not spawn');}) as never});
  await assert.rejects(runner.start('project',profile.id),/profile migration required/);
  assert.equal(spawned,false);
  assert.equal(runner.snapshot().configured,false);
 }finally{await rm(root,{recursive:true,force:true});}
});

test('actual compiler header outside fixed tracked list invalidates freshness',async()=>{
 const {root,p}=await fixture();
 try{
  await mkdir(root+'/studio');await rm(root+'/build',{recursive:true,force:true});
  await writeFile(root+'/CMakeLists.txt','cmake_minimum_required(VERSION 3.20)\nproject(Deps CXX)\nadd_executable(ARCH main.cpp)\nset_target_properties(ARCH PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")\n');
  await writeFile(root+'/main.cpp','#include "value.h"\nint main(){return value;}\n');
  await writeFile(root+'/value.h','constexpr int value=0;\n');
  await promisify(execFile)('/usr/bin/cmake',['-S',root,'-B',root+'/build','-G','Ninja'],{timeout:15000});
  const profile={...p,sourceRelativePath:undefined,trackedInputs:['CMakeLists.txt'],compilerDependencyMode:'ninja' as const};
  const runner=new BuildRunner(root,'p',profile);
  await runner.start('p',profile.id);await finished(runner);
  assert.equal(runner.snapshot().state,'succeeded');
  assert.ok(runner.snapshot().lastSuccessfulBuild?.compilerInputs?.files.some(f=>f.path===root+'/value.h'));
  await writeFile(root+'/value.h','constexpr int value=1;\n');
  const status=await runner.refreshFreshness();
  assert.equal(status.binaryState,'needs-build');assert.ok(status.changedInputs.includes(root+'/value.h'));
  // Losing optional evidence must not erase a positively known tracked change.
  await rm(root+'/build/.ninja_deps');
  await writeFile(root+'/CMakeLists.txt',(await readFile(root+'/CMakeLists.txt','utf8'))+'# changed input\n');
  const incomplete=await runner.refreshFreshness();
  assert.equal(incomplete.binaryState,'needs-build');
  assert.ok(incomplete.changedInputs.includes('CMakeLists.txt'));
 }finally{await rm(root,{recursive:true,force:true});}
});

test('persisted compiler evidence rejects corrupt hashes, paths and duplicate inputs',async()=>{
 const {root,p}=await fixture();
 try{
  await mkdir(root+'/studio');
  const runner=new BuildRunner(root,'p',p,{spawn:fakeSpawn(root)});
  await runner.start('p',p.id);await finished(runner);
  const m=runner.snapshot().lastSuccessfulBuild!;
  const file={path:root+'/case.cpp',sha256:'a'.repeat(64),size:10};
  const valid={...m,compilerInputs:{kind:'ninja-compiler-inputs' as const,objectCount:1,files:[file]},compilerInputsStableDuringBuild:true};
  await saveManifest(p,valid);assert.ok(await loadManifest(p));
  for(const files of [[{...file,sha256:'bad'}],[{...file,path:'relative'}],[file,file]]){
   await saveManifest(p,{...valid,compilerInputs:{...valid.compilerInputs,files}});
   assert.equal(await loadManifest(p),undefined);
  }
 }finally{await rm(root,{recursive:true,force:true});}
});

test('link-only input mutation invalidates build and missing linker inputs cannot claim current',async()=>{
 const {root,p}=await fixture();
 try{
  await mkdir(root+'/studio');
  await writeFile(root+'/library.so','library');
  await writeFile(root+'/build/ARCH.link.d','bin/ARCH: '+root+'/library.so '+root+'/missing.ltrans.o\n');
  const profile={...p,linkDependencyFile:'ARCH.link.d',dependenciesComplete:true};
  const runner=new BuildRunner(root,'p',profile,{spawn:fakeSpawn(root)});
  await runner.start('p',p.id);await finished(runner);
  assert.equal(runner.snapshot().state,'succeeded');
  assert.equal(runner.snapshot().binaryState,'freshness-unknown');
  assert.match(runner.snapshot().freshnessReason,/Linker input/);
  assert.match(runner.snapshot().freshnessReason,/1 recorded inputs are missing/);
  const manifest=await loadManifest(profile);assert.equal(manifest?.linkInputs?.unavailable.length,1);
  await writeFile(root+'/library.so','library changed');
  assert.equal((await runner.refreshFreshness()).binaryState,'needs-build');
  assert.ok(runner.snapshot().changedInputs.includes(root+'/library.so'));
  await saveManifest(profile,{...manifest!,linkInputs:{...manifest!.linkInputs!,depfileSha256:'bad'}});
  assert.equal(await loadManifest(profile),undefined);
 }finally{await rm(root,{recursive:true,force:true});}
});

test('compiler driver changes invalidate freshness independently of source changes',async()=>{
 const {root,p}=await fixture();
 try{
  await mkdir(root+'/studio');
  const reply=root+'/build/.cmake/api/v1/reply';
  await mkdir(reply,{recursive:true});
  await writeFile(root+'/compiler','driver-one');
  await writeFile(reply+'/toolchains.json',JSON.stringify({kind:'toolchains',version:{major:1},toolchains:[
   {language:'CXX',compiler:{path:root+'/compiler',id:'fixture',version:'1'}}
  ]}));
  await writeFile(reply+'/index-fixture.json',JSON.stringify({objects:[{kind:'toolchains',jsonFile:'toolchains.json'}]}));
  const profile={...p,compilerDependencyMode:'ninja' as const};
  const runner=new BuildRunner(root,'p',profile,{spawn:fakeSpawn(root)});
  await runner.start('p',profile.id);await finished(runner);
  assert.equal(runner.snapshot().lastSuccessfulBuild?.compilerDrivers?.length,1);
  await writeFile(root+'/compiler','driver-two');
  const changed=await runner.refreshFreshness();
  assert.equal(changed.binaryState,'needs-build');assert.ok(changed.changedInputs.includes(root+'/compiler'));
  await rm(root+'/compiler');
  const unknown=await runner.refreshFreshness();
  assert.equal(unknown.binaryState,'freshness-unknown');assert.match(unknown.freshnessReason,/Compiler toolchain identity/);
 }finally{await rm(root,{recursive:true,force:true});}
});

test('GNU subprocess/specs drift invalidates persisted Build identity and legacy evidence stays unknown',async()=>{
 const {root,p}=await fixture();
 try{
  await mkdir(root+'/studio');
  const reply=root+'/build/.cmake/api/v1/reply';await mkdir(reply,{recursive:true});
  await writeFile(reply+'/toolchains.json',JSON.stringify({kind:'toolchains',version:{major:1},toolchains:[
   {language:'CXX',compiler:{path:'/usr/bin/c++',id:'GNU',version:'13.3.0'}}
  ]}));
  await writeFile(reply+'/index-fixture.json',JSON.stringify({objects:[{kind:'toolchains',jsonFile:'toolchains.json'}]}));
  const profile={...p,compilerDependencyMode:'ninja' as const};
  const runner=new BuildRunner(root,'p',profile,{spawn:fakeSpawn(root)});
  await runner.start('p',profile.id);await finished(runner);
  const manifest=runner.snapshot().lastSuccessfulBuild!;
  assert.equal(manifest.compilerDrivers?.[0].components?.length,6);
  assert.equal(manifest.preBuildCompilerDrivers?.[0].components?.length,6);
  assert.equal(manifest.compilerDriversStableDuringBuild,true);
  for(const role of ['cc1plus','ld','liblto_plugin.so']){
   const mutated=structuredClone(manifest);
   mutated.compilerDrivers![0].components!.find(c=>c.role===role)!.sha256='a'.repeat(64);
   await saveManifest(profile,mutated);
   const reopened=new BuildRunner(root,'p',profile);
   const state=await reopened.initialize();
   assert.equal(state.binaryState,'needs-build');
   assert.ok(state.changedInputs.some(input=>input.includes(role==='ld'?'ld':role)));
  }
  const specs=structuredClone(manifest);specs.compilerDrivers![0].specsSha256='a'.repeat(64);
  await saveManifest(profile,specs);
  assert.equal((await new BuildRunner(root,'p',profile).initialize()).binaryState,'needs-build');
  const legacy=structuredClone(manifest);
  delete legacy.compilerDrivers![0].components;delete legacy.compilerDrivers![0].specsSha256;
  await saveManifest(profile,legacy);
  assert.equal((await new BuildRunner(root,'p',profile).initialize()).binaryState,'freshness-unknown');
  const invalid=structuredClone(manifest);
  invalid.compilerDrivers![0].components![0].path='relative';
  await saveManifest(profile,invalid);assert.equal(await loadManifest(profile),undefined);
 }finally{await rm(root,{recursive:true,force:true});}
});

test('toolchain mutation during build requires a new stable Build even when post-build driver matches disk',async()=>{
 const {root,p}=await fixture();
 try{
  await mkdir(root+'/studio');
  const reply=root+'/build/.cmake/api/v1/reply';await mkdir(reply,{recursive:true});
  await writeFile(root+'/compiler','before');
  await writeFile(reply+'/toolchains.json',JSON.stringify({kind:'toolchains',version:{major:1},toolchains:[
   {language:'CXX',compiler:{path:root+'/compiler',id:'fixture',version:'1'}}
  ]}));
  await writeFile(reply+'/index-fixture.json',JSON.stringify({objects:[{kind:'toolchains',jsonFile:'toolchains.json'}]}));
  const profile={...p,compilerDependencyMode:'ninja' as const};
  const runFake=fakeSpawn(root);
  const runner=new BuildRunner(root,'p',profile,{spawn:runFake});
  // Deterministic mutation at the Host-owned spawn boundary: pre-capture has already completed.
  const changing=new BuildRunner(root,'p',profile,{spawn:((...args:Parameters<typeof runFake>)=>{
   const child=runFake(...args);
   writeFileSync(root+'/compiler','after');
   return child;
  }) as never});
  await changing.start('p',profile.id);await finished(changing);
  assert.equal(changing.snapshot().state,'succeeded');
  assert.equal(changing.snapshot().lastSuccessfulBuild?.compilerDriversStableDuringBuild,false);
  assert.equal(changing.snapshot().binaryState,'needs-build');
  assert.equal((await new BuildRunner(root,'p',profile).initialize()).binaryState,'needs-build');
  await runner.start('p',profile.id);await finished(runner);
  assert.equal(runner.snapshot().lastSuccessfulBuild?.compilerDriversStableDuringBuild,true);
  const legacy=structuredClone(runner.snapshot().lastSuccessfulBuild!);
  delete legacy.compilerDriversStableDuringBuild;delete legacy.preBuildCompilerDrivers;
  await saveManifest(profile,legacy);
  assert.equal((await new BuildRunner(root,'p',profile).initialize()).binaryState,'freshness-unknown');
  await writeFile(root+'/compiler','changed after legacy build');
  assert.equal((await new BuildRunner(root,'p',profile).initialize()).binaryState,'needs-build');
  const malformed={...legacy,compilerDriversStableDuringBuild:'yes'} as never;
  await saveManifest(profile,malformed);assert.equal(await loadManifest(profile),undefined);
 }finally{await rm(root,{recursive:true,force:true});}
});
