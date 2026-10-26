/** Cross-host A1 controls use actual host paths, never an emulated platform. */
import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {tmpdir} from 'node:os';
import {mkdtemp,mkdir,writeFile,readFile,rm,realpath,open} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {readCMakeConfigurationEvidence,readBuildGeneratorEvidence,readBuildLinkerSelection} from '../host/cmakeEvidence.ts';

const windows=process.platform==='win32';
const cmakeSpelling=(value:string)=>windows?value.replaceAll('\\','/'):value;
const sha=(value:Buffer|string)=>createHash('sha256').update(value).digest('hex');
async function fixture(){
 const temp=await mkdtemp(path.join(tmpdir(),'ARCH A1 中文 space-'));
 const root=await realpath(temp),source=path.join(root,'source'),build=path.join(root,'build'),reply=path.join(build,'.cmake/api/v1/reply');
 await mkdir(source);await mkdir(reply,{recursive:true});
 const input=path.join(source,'CMakeLists.txt'),file=path.join(reply,'cmakeFiles.json');
 await writeFile(input,'cmake_minimum_required(VERSION 3.20)\nproject(A1 NONE)\n');
 const data={kind:'cmakeFiles',version:{major:1},paths:{source:cmakeSpelling(source),build:cmakeSpelling(build)},inputs:[{path:'CMakeLists.txt'}]};
 const put=(value:unknown)=>writeFile(file,JSON.stringify(value));await put(data);
 return {root,source,build,reply,input,file,data,put,read:()=>readCMakeConfigurationEvidence(source,build,file),cleanup:()=>rm(root,{recursive:true,force:true})};
}

test('CMake path spelling preserves raw reply and file SHA identities',async()=>{
 const f=await fixture();
 try{
  const first=await f.read();assert.equal(first.replySha256,sha(await readFile(f.file)));
  assert.equal(first.sourceRoot,f.source);assert.equal(first.buildDirectory,f.build);
  assert.equal(first.inputs[0].path,f.input);assert.equal(first.inputs[0].sha256,sha(await readFile(f.input)));
  assert.equal(first.dependenciesComplete,false);assert.deepEqual(first.missingCoverage,['compiler-includes','link-inputs','toolchain-identity']);
  await f.put({...f.data,paths:{source:f.source,build:f.build}});
  const native=await f.read();assert.deepEqual(native.inputs,first.inputs);
  if(windows)assert.notEqual(native.replySha256,first.replySha256,'raw reply bytes retain their own fingerprint');
  if(windows){
   await f.put({...f.data,paths:{source:f.source+'\\.',build:f.data.paths.build}});
   assert.deepEqual((await f.read()).inputs,first.inputs,'approved path.normalize includes lexical dot removal');
   await f.put({...f.data,paths:{source:f.data.paths.source,build:f.data.paths.build.replace(/^[a-z]:/i,drive=>drive.toLowerCase())}});
   assert.deepEqual((await f.read()).inputs,first.inputs,'CMake cache may lowercase only the drive letter');
  }
  await writeFile(f.input,'changed input\n');assert.notEqual((await f.read()).inputs[0].sha256,first.inputs[0].sha256);
  if(!windows){await f.put({...f.data,paths:{source:f.source+'/.',build:f.build}});await assert.rejects(f.read(),/binding/);}
 }finally{await f.cleanup();}
});

test('normalization still rejects wrong, relative, NUL and drive-relative bindings',async()=>{
 const f=await fixture();
 try{
  for(const source of [f.root,f.source+'-other','relative','C:relative',f.source+'\0']){
   await f.put({...f.data,paths:{source,build:f.data.paths.build}});await assert.rejects(f.read(),/binding/);
  }
  for(const build of [f.root,f.build+'-other','relative','C:relative',f.build+'\0']){
   await f.put({...f.data,paths:{source:f.data.paths.source,build}});await assert.rejects(f.read(),/binding/);
  }
  if(windows){
   await f.put({...f.data,paths:{source:path.join(f.root,'Source'),build:f.data.paths.build}});
   await assert.rejects(f.read(),/binding/,'A1 does not fold directory or file case');
  }
  await f.put({...f.data,version:{major:2}});await assert.rejects(f.read(),/Incompatible/);
  await f.put({...f.data,inputs:[{path:'missing.cmake'}]});await assert.rejects(f.read());
  await f.put({...f.data,inputs:[{path:'../outside.cmake'}]});await assert.rejects(f.read(),/escapes/);
 }finally{await f.cleanup();}
});

test('reply ownership and input size checks remain active',async()=>{
 const f=await fixture();
 try{
  const outside=path.join(f.root,'outside.json');await writeFile(outside,JSON.stringify(f.data));
  await assert.rejects(readCMakeConfigurationEvidence(f.source,f.build,outside),/outside/);
  const large=await open(f.input,'r+');try{await large.truncate(16*1024*1024+1);}finally{await large.close();}
  await assert.rejects(f.read(),/over budget/);
 }finally{await f.cleanup();}
});

test('generator path comparisons retain fixed-tool binding, hashes and duplicate rejection',async()=>{
 const f=await fixture();
 try{
  const cmake=path.join(f.root,'cmake.exe'),ninja=path.join(f.root,'ninja.exe');
  // This reader fingerprints tools; these text fixtures must never be executed.
  await writeFile(cmake,'not an executable\n',{mode:0o700});await writeFile(ninja,'ninja-v1\n',{mode:0o700});
  const values={CMAKE_HOME_DIRECTORY:cmakeSpelling(f.source),CMAKE_CACHEFILE_DIR:cmakeSpelling(f.build),CMAKE_GENERATOR:'Ninja',CMAKE_COMMAND:cmakeSpelling(cmake),CMAKE_MAKE_PROGRAM:cmakeSpelling(ninja)};
  const put=(v=values,extra='')=>writeFile(path.join(f.build,'CMakeCache.txt'),Object.entries(v).map(([k,x])=>k+':INTERNAL='+x).join('\n')+'\n'+extra);
  await put();const first=await readBuildGeneratorEvidence(f.source,f.build,cmake);
  assert.equal(first[0].sha256,sha(await readFile(cmake)));assert.equal(first[1].sha256,sha(await readFile(ninja)));
  await writeFile(ninja,'ninja-v2\n');assert.notEqual((await readBuildGeneratorEvidence(f.source,f.build,cmake))[1].sha256,first[1].sha256);
  await put({...values,CMAKE_HOME_DIRECTORY:cmakeSpelling(f.root)});await assert.rejects(readBuildGeneratorEvidence(f.source,f.build,cmake),/binding/);
  await put({...values,CMAKE_COMMAND:cmakeSpelling(ninja)});await assert.rejects(readBuildGeneratorEvidence(f.source,f.build,cmake),/differs/);
  await put(values,'CMAKE_GENERATOR:INTERNAL=Ninja\n');await assert.rejects(readBuildGeneratorEvidence(f.source,f.build,cmake),/Duplicate/);
  await put({...values,CMAKE_MAKE_PROGRAM:'relative'});await assert.rejects(readBuildGeneratorEvidence(f.source,f.build,cmake),/Absolute/);
 }finally{await f.cleanup();}
});

test('codemodel host spelling preserves target binding and linker override rejection',async()=>{
 const f=await fixture();
 try{
  await writeFile(path.join(f.reply,'index-a1.json'),JSON.stringify({objects:[{kind:'codemodel',jsonFile:'model.json'}]}));
  const model={kind:'codemodel',version:{major:2},paths:{build:cmakeSpelling(f.build)},configurations:[{targets:[{name:'ARCH',jsonFile:'target.json'}]}]};
  const put=(v:unknown)=>writeFile(path.join(f.reply,'model.json'),JSON.stringify(v));await put(model);
  const target=path.join(f.reply,'target.json');
  const flags=(value:string)=>writeFile(target,JSON.stringify({name:'ARCH',type:'EXECUTABLE',link:{language:'CXX',commandFragments:[{role:'flags',fragment:value}]}}));
  await flags('-O3');const first=await readBuildLinkerSelection(f.build);assert.equal(first.programName,'ld');assert.equal(first.observedExecution,false);
  assert.equal(first.targetReplySha256,sha(await readFile(target)));
  await put({...model,paths:{build:f.build+'-other'}});await assert.rejects(readBuildLinkerSelection(f.build),/binding/);
  await put(model);await flags('-Bbad');await assert.rejects(readBuildLinkerSelection(f.build),/override/);
  await flags('-O3');await put({...model,configurations:[{targets:[{name:'ARCH',jsonFile:'../outside.json'}]}]});await assert.rejects(readBuildLinkerSelection(f.build),/target/);
 }finally{await f.cleanup();}
});
