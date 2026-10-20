import test from 'node:test';import assert from 'node:assert/strict';
import {registeredSourceCase} from '../host/desktopSource.ts';
test('desktop source association accepts real absolute and relative registry paths',()=>{
 const absolute=[{caseId:'Sod',inspection:{sourceFile:'/project/simulation/Sod/Sod.cpp'}}];
 assert.equal(registeredSourceCase(absolute,'/project','simulation/Sod/Sod.cpp'),'Sod');
 assert.equal(registeredSourceCase([{caseId:'Sod',inspection:{sourceFile:'simulation/Sod/Sod.cpp'}}],'/project','/project/simulation/Sod/Sod.cpp','Sod'),'Sod');
 assert.throws(()=>registeredSourceCase(absolute,'/project','other/Sod.cpp'));
 assert.throws(()=>registeredSourceCase(absolute,'/project','../other/Sod.cpp'));
 assert.throws(()=>registeredSourceCase(absolute,'/project','simulation/Sod/Sod.cpp','CellularDet'));
 assert.throws(()=>registeredSourceCase([{caseId:'Sod',inspection:{sourceFile:null}}],'/project','simulation/Sod/Sod.cpp'));
 assert.throws(()=>registeredSourceCase([...absolute,...absolute],'/project','simulation/Sod/Sod.cpp'));
});

import {desktopRegistry} from '../host/desktopSource.ts';
import {previewFixture} from './preview-fixture.ts';
import {ConfigurationAdapter} from '../host/configuration.ts';
import {WorkflowRunner} from '../host/workflow.ts';
import {readFile,writeFile} from 'node:fs/promises';
test('desktop static registry opens stale selected binary without enabling initialization',async()=>{
 const f=await previewFixture();
 try{
  const registry=JSON.parse(await readFile(new URL('../../src/api/examples/local-workflow/registered-cases.json',import.meta.url),'utf8'));
  const script='#!'+process.execPath+'\nif(process.argv[2]!=="--list-cases")process.exit(2);console.log('+JSON.stringify(JSON.stringify(registry))+');\n';
  await writeFile(f.root+'/build/bin/ARCH',script,{mode:0o755});
  await writeFile(f.root+'/case.cpp','changed tracked source');
  const configuration=new ConfigurationAdapter({root:f.root,projectId:'p',binaryRelativePath:'build/bin/ARCH'});
  const workflow=new WorkflowRunner(f.preview);
  const result=await desktopRegistry({configuration,workflow});
  assert.ok(result.cases.some(c=>c.caseId==='Sod'));
  assert.ok(result.buildId.startsWith('selected-binary:'));
  assert.deepEqual(result.fieldModels,[]);assert.equal(result.amr,null);
  assert.equal((await f.preview.readiness()).ready,false);
  await assert.rejects(workflow.discovery(),/Build required/);
  await assert.rejects(f.preview.start(f.request),/Build required/);
 }finally{await f.cleanup();}
});

import {openDesktopProject} from '../host/desktopProject.ts';
import {mkdtemp,mkdir,rm,symlink} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';

test('desktop workbench opens an unconfigured project without fabricating binary readiness',async t=>{
 const root=await mkdtemp(path.join(tmpdir(),'arch desktop bootstrap-'));t.after(()=>rm(root,{recursive:true,force:true}));
 await mkdir(root+'/simulation');await writeFile(root+'/CMakeLists.txt','project(ARCH)\n');await writeFile(root+'/CMakePresets.json','{}');
 const opened=await openDesktopProject({project:root,cwd:root});t.after(()=>opened.reader.configure?.shutdown());
 assert.equal(opened.root,root);assert.equal(opened.caseId,'Sod');assert.equal(opened.registrationPending,true);
 assert.equal(opened.reader.snapshot().session.binaryState,'missing');assert.equal(opened.reader.build?.snapshot().configured,false);
 assert.ok(opened.reader.configure);assert.equal((await opened.reader.preview!.readiness()).ready,false);
 await assert.rejects(opened.reader.configuration!.discovery(),/missing|unavailable|ENOENT/i);
 await assert.rejects(openDesktopProject({project:root,cwd:root,binary:'unapproved/ARCH'}),/approved/);
 assert.equal((await readFile(root+'/CMakeLists.txt','utf8')),'project(ARCH)\n');
});

test('desktop workbench retains an explicitly selected missing Host-owned binary',async t=>{
 const root=await mkdtemp(path.join(tmpdir(),'arch desktop missing-'));t.after(()=>rm(root,{recursive:true,force:true}));
 await mkdir(root+'/simulation');await writeFile(root+'/CMakeLists.txt','project(ARCH)\n');await writeFile(root+'/CMakePresets.json','{}');
 const opened=await openDesktopProject({project:root,cwd:root,binary:'build-studio-cpu/bin/ARCH',caseId:'PendingCase'});
 t.after(()=>opened.reader.configure?.shutdown());assert.equal(opened.caseId,'PendingCase');assert.equal(opened.registrationPending,true);
 assert.equal(opened.reader.snapshot().session.mapping,'unknown');assert.equal(opened.reader.snapshot().host.capabilities.preview,false);
});

import {existingBuildProfile,validateExistingBuildProfile} from '../host/existingBuildProfile.ts';
import {requireCompiledSourceCase} from '../host/desktopSource.ts';
import {createHash} from 'node:crypto';

async function existingTree(t:{after:(f:()=>unknown)=>void},cuda='OFF'){
 const root=await mkdtemp(path.join(tmpdir(),'arch existing profile-'));t.after(()=>rm(root,{recursive:true,force:true}));
 await mkdir(root+'/simulation',{recursive:true});await mkdir(root+'/chosen/CMakeFiles',{recursive:true});await mkdir(root+'/chosen/bin');
 await writeFile(root+'/CMakeLists.txt','project(ARCH)\n');await writeFile(root+'/CMakePresets.json','{}');
 const cache=['CMAKE_HOME_DIRECTORY:INTERNAL='+root,'CMAKE_CACHEFILE_DIR:INTERNAL='+root+'/chosen',
  'CMAKE_GENERATOR:INTERNAL=Ninja','ARCH_ENABLE_CUDA:BOOL='+cuda,'ARCH_RUNTIME_OUTPUT_DIRECTORY:PATH='+root+'/chosen/bin'].join('\n')+'\n';
 await writeFile(root+'/chosen/CMakeCache.txt',cache);await writeFile(root+'/chosen/build.ninja','build ARCH: phony bin/ARCH\n');
 await writeFile(root+'/chosen/CMakeFiles/TargetDirectories.txt',root+'/chosen/CMakeFiles/ARCH.dir\n');return {root,cache};
}

test('explicit existing build preserves CUDA mode and does not invent successful provenance',async t=>{
 const {root}=await existingTree(t,'ON');const profile=await existingBuildProfile(root,'chosen');
 assert.equal(profile.build.buildBackend,'cuda');assert.deepEqual(profile.configure.definitions,{});
 const opened=await openDesktopProject({project:root,cwd:root,buildDir:'chosen'});t.after(()=>opened.reader.configure?.shutdown());
 assert.equal(opened.reader.build?.profile.outputBinaryRelative,'chosen/bin/ARCH');assert.equal(opened.registrationPending,true);
 assert.equal(opened.reader.build?.snapshot().lastSuccessfulBuild,undefined);assert.equal((await opened.reader.preview!.readiness()).ready,false);
 await validateExistingBuildProfile(root,profile.build);
 await writeFile(root+'/chosen/CMakeCache.txt',(await readFile(root+'/chosen/CMakeCache.txt','utf8')).replace('CUDA:BOOL=ON','CUDA:BOOL=OFF'));
 await assert.rejects(validateExistingBuildProfile(root,profile.build),/changed/);
 const rejected=await opened.reader.configure!.run(opened.reader.snapshot().session.projectId,profile.build.id);
 assert.equal(rejected.state,'failed');assert.match(rejected.error??'',/changed/);assert.equal(opened.reader.configure!.processId,undefined);
});

test('existing build rejects wrong binding, generator, target, output, ambiguity and symlink',async t=>{
 const {root,cache}=await existingTree(t);
 for(const replacement of [cache.replace('HOME_DIRECTORY:INTERNAL='+root,'HOME_DIRECTORY:INTERNAL=/other'),
  cache.replace('GENERATOR:INTERNAL=Ninja','GENERATOR:INTERNAL=Unix Makefiles'),
  cache.replace('CUDA:BOOL=OFF','CUDA:BOOL=auto'),cache+'ARCH_ENABLE_CUDA:BOOL=ON\n',
  cache.replace('OUTPUT_DIRECTORY:PATH='+root+'/chosen/bin','OUTPUT_DIRECTORY:PATH=/tmp')]){
  await writeFile(root+'/chosen/CMakeCache.txt',replacement);await assert.rejects(existingBuildProfile(root,'chosen'));
 }
 await writeFile(root+'/chosen/CMakeCache.txt',cache);
 await assert.rejects(existingBuildProfile(root,'chosen','other/ARCH'),/configured ARCH output/);
 await writeFile(root+'/chosen/CMakeFiles/TargetDirectories.txt','');await assert.rejects(existingBuildProfile(root,'chosen'),/ARCH target/);
 await writeFile(root+'/chosen/CMakeFiles/TargetDirectories.txt',root+'/chosen/CMakeFiles/ARCH.dir\n');
 await symlink(root+'/chosen',root+'/alias');await assert.rejects(existingBuildProfile(root,'alias'),/Symlinks/);
 await assert.rejects(existingBuildProfile(root,'../elsewhere'));
});

test('new simulation source remains pending with no filename or registration inference',async t=>{
 const {root}=await existingTree(t);await mkdir(root+'/simulation/NewCase');await writeFile(root+'/simulation/NewCase/case.cpp','not parsed or edited');
 const opened=await openDesktopProject({project:root,cwd:root,source:'simulation/NewCase/case.cpp'});t.after(()=>opened.reader.configure?.shutdown());
 assert.equal(opened.registrationPending,true);assert.equal(opened.reader.snapshot().session.caseSource?.relativePath,'simulation/NewCase/case.cpp');
 assert.equal(opened.reader.build?.profile.caseId,undefined);assert.equal(opened.reader.snapshot().session.mapping,'unknown');
 assert.ok(opened.reader.build?.profile.trackedInputs.includes('simulation/NewCase/case.cpp'));
 await writeFile(root+'/other.cpp','x');await assert.rejects(openDesktopProject({project:root,cwd:root,source:'other.cpp'}),/simulation/);
 await symlink(root+'/simulation/NewCase/case.cpp',root+'/simulation/linked.cpp');await assert.rejects(openDesktopProject({project:root,cwd:root,source:'simulation/linked.cpp'}),/Symlinks/);
});

test('exact compiled source guard accepts unique identity and rejects edits, duplicates and wrong case',async t=>{
 const {root}=await existingTree(t);await mkdir(root+'/simulation/New');const source='simulation/New/case.cpp';await writeFile(root+'/'+source,'frozen source');
 const registry=JSON.parse(await readFile(new URL('../../src/api/examples/local-workflow/registered-cases.json',import.meta.url),'utf8'));
 const entry=structuredClone(registry.cases[0]);entry.caseId='New';entry.inspection.sourceFile=root+'/'+source;
 entry.inspection.compiledSourceSha256=createHash('sha256').update('frozen source').digest('hex');
 assert.equal(await requireCompiledSourceCase(root,source,[entry]),'New');
 await assert.rejects(requireCompiledSourceCase(root,source,[entry],'Sod'),/disagree/);
 await assert.rejects(requireCompiledSourceCase(root,source,[entry,entry]),/unique/);
 await assert.rejects(requireCompiledSourceCase(root,source,[]),/unique/);
 await writeFile(root+'/'+source,'changed source');await assert.rejects(requireCompiledSourceCase(root,source,[entry]),/compiled registration/);
});
