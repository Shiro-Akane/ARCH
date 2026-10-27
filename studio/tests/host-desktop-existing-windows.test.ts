import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {tmpdir} from 'node:os';
import {mkdtemp,mkdir,realpath,rm,writeFile} from 'node:fs/promises';
import {existingBuildProfile,validateExistingBuildProfile} from '../host/existingBuildProfile.ts';
import {profileFingerprint} from '../host/buildProfile.ts';
import type {WindowsHostTools} from '../host/hostProcess.ts';

const native=process.platform==='win32';
function nativeTools():WindowsHostTools{
 return {cmake:'C:/arch-tools/cmake.exe',supervisor:'C:/arch-tools/supervisor.exe',ninja:'C:/arch-tools/ninja.exe',path:'C:/arch-tools'};
}
function windowsSpelling(value:string){
 return value.replace(/\\/g,'/').replace(/^([A-Za-z]):/,(_,drive:string)=>drive.toLowerCase()+':');
}
type CacheOptions={nativeSpelling?:boolean;cuda?:'ON'|'OFF';emit?:'ON'|'OFF';retain?:'ON'|'OFF';
 source?:string;cacheDir?:string;output?:string;generator?:string;extra?:string[]};
type FixtureOptions=CacheOptions&{ninja?:string;targetDirs?:string};

async function fixture(t:{after:(fn:()=>unknown)=>void},options:FixtureOptions={}){
 const root=await realpath(await mkdtemp(path.join(tmpdir(),'arch existing windows-')));
 t.after(()=>rm(root,{recursive:true,force:true}));
 const build=path.join(root,'chosen'),cachePath=path.join(build,'CMakeCache.txt');
 const targetPath=path.join(build,'CMakeFiles','TargetDirectories.txt'),ninjaPath=path.join(build,'build.ninja');
 const spell=(value:string)=>options.nativeSpelling?windowsSpelling(value):value;
 await mkdir(path.join(build,'CMakeFiles'),{recursive:true});await mkdir(path.join(build,'bin'));
 await writeFile(path.join(root,'CMakeLists.txt'),'project(ARCH)\n');await writeFile(path.join(root,'CMakePresets.json'),'{}');
 const writeCache=async(overrides:CacheOptions={})=>{
  const next={...options,...overrides};
  const spellNext=(value:string)=>next.nativeSpelling?windowsSpelling(value):value;
  const lines=[
   'CMAKE_HOME_DIRECTORY:INTERNAL='+(next.source??spellNext(root)),
   'CMAKE_CACHEFILE_DIR:INTERNAL='+(next.cacheDir??spellNext(build)),
   'CMAKE_GENERATOR:INTERNAL='+(next.generator??'Ninja'),
   'ARCH_ENABLE_CUDA:BOOL='+(next.cuda??'OFF'),
   'ARCH_RUNTIME_OUTPUT_DIRECTORY:PATH='+(next.output??spellNext(path.join(build,'bin'))),
   'ARCH_EMIT_LINK_DEPENDENCIES:BOOL='+(next.emit??'ON'),
   'ARCH_RETAIN_LTO_LINK_INPUTS:BOOL='+(next.retain??'ON')
  ];
  await writeFile(cachePath,lines.join('\n')+'\n'+(next.extra?.length?next.extra.join('\n')+'\n':''));
 };
 await writeCache();
 await writeFile(ninjaPath,options.ninja??(options.nativeSpelling?
  'build ARCH: phony bin/ARCH.exe\nbuild ARCH.exe: phony bin/ARCH.exe\n':'build ARCH: phony bin/ARCH\n'));
 await writeFile(targetPath,options.targetDirs??spell(build)+'/CMakeFiles/ARCH.dir\n');
 return {root,build,cachePath,targetPath,ninjaPath,spell,writeCache};
}

if(process.platform!=='win32') test('Linux existing binding keeps exact prior output and never enables observed scope',{skip:native},async t=>{
 const f=await fixture(t,{emit:'ON',retain:'ON'});
 const basic=await existingBuildProfile(f.root,'chosen');
 assert.equal(basic.build.outputBinaryRelative,'chosen/bin/ARCH');
 assert.equal(basic.build.target,'ARCH');
 assert.equal(basic.build.buildBackend,'cpu');
 assert.equal(basic.build.observedInputScope,undefined);
 assert.equal(basic.build.dependenciesComplete,false);
 assert.equal(basic.build.linkDependencyFile,'ARCH.link.d');
 assert.equal(basic.build.retainGnuLtoInputs,true);
 assert.deepEqual(basic.configure.definitions,{});
 assert.equal('windowsTools' in basic.build,false);
 const tools=nativeTools();
 const withTools=await existingBuildProfile(f.root,'chosen',undefined,tools);
 assert.equal(withTools.build.outputBinaryRelative,'chosen/bin/ARCH');
 assert.equal(withTools.build.observedInputScope,undefined);
 assert.equal(withTools.build.dependenciesComplete,false);
 assert.deepEqual(withTools.configure.windowsTools,tools);
 assert.equal('windowsTools' in withTools.build,false);
 assert.equal(profileFingerprint(withTools.build),profileFingerprint(basic.build));
 const selected={...basic.build,sourceRelativePath:'simulation/Sod/Sod.cpp',trackedInputs:[...basic.build.trackedInputs,'simulation/Sod/Sod.cpp']};
 await validateExistingBuildProfile(f.root,basic.build);
 await validateExistingBuildProfile(f.root,withTools.build,tools);
 await validateExistingBuildProfile(f.root,selected,tools);
});

if(process.platform==='win32') test('native existing binding normalizes slash/drive spelling and binds exact ARCH.exe',{skip:!native},async t=>{
 const f=await fixture(t,{nativeSpelling:true});const tools=nativeTools();
 const profile=await existingBuildProfile(f.root,'chosen','chosen/bin/ARCH.exe',tools);
 assert.equal(profile.build.managedSourceRoot,f.root);
 assert.equal(profile.build.outputBinaryRelative,'chosen/bin/ARCH.exe');
 assert.equal(profile.build.target,'ARCH');
 assert.equal(profile.build.buildBackend,'cpu');
 assert.equal(profile.build.observedInputScope,'observed-build-inputs-v1');
 assert.equal(profile.build.dependenciesComplete,false);
 assert.equal(profile.build.linkDependencyFile,'ARCH.link.d');
 assert.equal(profile.build.retainGnuLtoInputs,true);
 assert.deepEqual(profile.configure.definitions,{});
 assert.deepEqual(profile.configure.windowsTools,tools);
 assert.equal('windowsTools' in profile.build,false);
 assert.equal(JSON.stringify(profile.build).includes('arch-tools'),false);
 await assert.rejects(existingBuildProfile(f.root,'chosen','chosen/bin/ARCH',tools),/configured ARCH output/);
 await assert.rejects(existingBuildProfile(f.root,'chosen','chosen/bin/arch.exe',tools),/configured ARCH output/);
});

if(process.platform==='win32') test('native existing binding rejects changed spelling, duplicates, generator and targets',{skip:!native},async t=>{
 const f=await fixture(t,{nativeSpelling:true});const tools=nativeTools();
 const wrongCase=f.spell(f.root).replace('arch existing windows-','ARCH EXISTING WINDOWS-');
 await f.writeCache({nativeSpelling:true,source:wrongCase});
 await assert.rejects(existingBuildProfile(f.root,'chosen',undefined,tools),/binding/,'directory and file case stays strict');
 await f.writeCache({nativeSpelling:true,source:f.spell(f.root)+'-other'});
 await assert.rejects(existingBuildProfile(f.root,'chosen',undefined,tools),/binding/);
 await f.writeCache({nativeSpelling:true,extra:['ARCH_ENABLE_CUDA:BOOL=OFF']});
 await assert.rejects(existingBuildProfile(f.root,'chosen',undefined,tools),/Duplicate CMake cache key/);
 await f.writeCache({nativeSpelling:true,generator:'Unix Makefiles'});
 await assert.rejects(existingBuildProfile(f.root,'chosen',undefined,tools),/Ninja generator/);
 await f.writeCache({nativeSpelling:true,emit:'OFF',retain:'ON'});
 await assert.rejects(existingBuildProfile(f.root,'chosen',undefined,tools),/linker dependency authority/);
 await f.writeCache({nativeSpelling:true});
 await writeFile(f.targetPath,'');
 await assert.rejects(existingBuildProfile(f.root,'chosen',undefined,tools),/ARCH target/);
 await writeFile(f.targetPath,f.spell(f.build)+'/CMakeFiles/other.dir\n');
 await assert.rejects(existingBuildProfile(f.root,'chosen',undefined,tools),/ARCH target/);
 await writeFile(f.targetPath,f.spell(f.build)+'/CMakeFiles/ARCH.dir\n');
 await writeFile(f.ninjaPath,'build ARCH.exe: phony bin/ARCH.exe\n');
 await assert.rejects(existingBuildProfile(f.root,'chosen',undefined,tools),/Ninja ARCH target/);
});

if(process.platform==='win32') test('native recheck with the same tools retains identity and rejects a backend flip',{skip:!native},async t=>{
 const f=await fixture(t,{nativeSpelling:true});const tools=nativeTools();
 const profile=await existingBuildProfile(f.root,'chosen','chosen/bin/ARCH.exe',tools);
 const recreated=await existingBuildProfile(f.root,'chosen','chosen/bin/ARCH.exe',tools);
 assert.equal(profileFingerprint(recreated.build),profileFingerprint(profile.build));
 const selected={...profile.build,sourceRelativePath:'simulation/Sod/Sod.cpp',trackedInputs:[...profile.build.trackedInputs,'simulation/Sod/Sod.cpp']};
 await validateExistingBuildProfile(f.root,profile.build,tools);
 await validateExistingBuildProfile(f.root,selected,tools);
 await f.writeCache({nativeSpelling:true,cuda:'ON'});
 await assert.rejects(validateExistingBuildProfile(f.root,profile.build,tools),/binding changed/);
});

if(process.platform==='win32') test('native observed scope enables only for CPU emit/retain with trusted tools',{skip:!native},async t=>{
 const f=await fixture(t,{nativeSpelling:true});const tools=nativeTools();
 const enabled=await existingBuildProfile(f.root,'chosen','chosen/bin/ARCH.exe',tools);
 assert.equal(enabled.build.observedInputScope,'observed-build-inputs-v1');
 assert.equal(enabled.build.dependenciesComplete,false);
 await f.writeCache({nativeSpelling:true,emit:'OFF',retain:'OFF'});
 const noLinkEvidence=await existingBuildProfile(f.root,'chosen','chosen/bin/ARCH.exe',tools);
 assert.equal(noLinkEvidence.build.observedInputScope,undefined);
 assert.equal(noLinkEvidence.build.dependenciesComplete,false);
 assert.equal(noLinkEvidence.build.linkDependencyFile,undefined);
 await f.writeCache({nativeSpelling:true,emit:'ON',retain:'OFF'});
 const noRetention=await existingBuildProfile(f.root,'chosen','chosen/bin/ARCH.exe',tools);
 assert.equal(noRetention.build.observedInputScope,undefined);
 assert.equal(noRetention.build.dependenciesComplete,false);
 await f.writeCache({nativeSpelling:true,cuda:'ON'});
 const cuda=await existingBuildProfile(f.root,'chosen','chosen/bin/ARCH.exe',tools);
 assert.equal(cuda.build.observedInputScope,undefined);
 assert.equal(cuda.build.dependenciesComplete,false);
 await f.writeCache({nativeSpelling:true});
 const noTools=await existingBuildProfile(f.root,'chosen','chosen/bin/ARCH.exe');
 assert.equal(noTools.build.observedInputScope,undefined);
 assert.equal(noTools.build.dependenciesComplete,false);
 assert.notEqual(profileFingerprint(noTools.build),profileFingerprint(enabled.build));
});
