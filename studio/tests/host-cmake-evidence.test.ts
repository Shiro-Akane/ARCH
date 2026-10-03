import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,rm,readdir} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {readCMakeConfigurationEvidence,readCMakeToolchainEvidence,readBuildConfigurationInputs,sameConfigurationInputs} from '../host/cmakeEvidence.ts';
test('CMake configuration evidence binds source/build and hashes external inputs without claiming compiler coverage',async()=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch cmake evidence-'));
 try{
  const source=root+'/source',build=root+'/build',reply=build+'/.cmake/api/v1/reply';
  await mkdir(source);await mkdir(reply,{recursive:true});
  await writeFile(source+'/CMakeLists.txt','project(example)');
  await writeFile(root+'/dependency.cmake','# external');
  const file=reply+'/cmakeFiles-v1.json';
  const data={kind:'cmakeFiles',version:{major:1,minor:0},paths:{source,build},inputs:[{path:'CMakeLists.txt'},{path:root+'/dependency.cmake',isExternal:true}]};
  const read=()=>readCMakeConfigurationEvidence(source,build,file);
  await writeFile(file,JSON.stringify(data));
  const first=await read();assert.equal(first.inputs.length,2);assert.equal(first.dependenciesComplete,false);
  assert.ok(first.missingCoverage.includes('compiler-includes'));
  await writeFile(root+'/dependency.cmake','# modified external');
  assert.notDeepEqual((await read()).inputs,first.inputs);
  await writeFile(file,JSON.stringify({...data,paths:{source:root,build}}));
  await assert.rejects(read(),/binding/);
  await writeFile(file,JSON.stringify({...data,inputs:[{path:'../dependency.cmake'}]}));
  await assert.rejects(read(),/escapes/);
  await writeFile(file,JSON.stringify({...data,inputs:[{path:'missing.cmake'}]}));
  await assert.rejects(read());
  await writeFile(file,JSON.stringify({...data,version:{major:2}}));
  await assert.rejects(read(),/Incompatible/);
 }finally{await rm(root,{recursive:true,force:true});}
});

test('reads actual installed CMake File API with a spaced source/build path',async()=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch actual cmake-'));
 try{
  const source=root+'/source tree',build=root+'/build tree';
  await mkdir(source);await mkdir(build+'/.cmake/api/v1/query',{recursive:true});
  await writeFile(source+'/CMakeLists.txt','cmake_minimum_required(VERSION 3.20)\nproject(Evidence NONE)\n');
  await writeFile(build+'/.cmake/api/v1/query/cmakeFiles-v1','');
  await promisify(execFile)('/usr/bin/cmake',['-S',source,'-B',build],{timeout:15000,maxBuffer:1024*1024});
  const reply=build+'/.cmake/api/v1/reply';
  const name=(await readdir(reply)).find(n=>n.startsWith('cmakeFiles-v1-'));
  assert.ok(name);
  const evidence=await readCMakeConfigurationEvidence(source,build,reply+'/'+name);
  assert.ok(evidence.inputs.some(i=>i.path===source+'/CMakeLists.txt'));
  assert.ok(evidence.inputs.some(i=>i.cmake&&i.external));
  assert.equal(evidence.dependenciesComplete,false);
  assert.equal(sameConfigurationInputs(evidence,await readBuildConfigurationInputs(source,build)),true);
  assert.equal(sameConfigurationInputs(undefined,evidence),false);
 }finally{await rm(root,{recursive:true,force:true});}
});

test('toolchain evidence hashes the selected driver and rejects invalid versions, bindings and missing files',async()=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch toolchain-'));
 try{
  const build=root+'/build',reply=build+'/.cmake/api/v1/reply';
  await mkdir(reply,{recursive:true});await writeFile(root+'/compiler','driver-v1');
  const file=reply+'/toolchains.json';
  const data={kind:'toolchains',version:{major:1,minor:0},toolchains:[{language:'CXX',compiler:{path:root+'/compiler',id:'test',version:'1'}}]};
  await writeFile(file,JSON.stringify(data));
  const first=await readCMakeToolchainEvidence(build,file);
  assert.equal(first.compilers[0].size,9);assert.equal(first.dependenciesComplete,false);
  await writeFile(root+'/compiler','driver-v2');
  assert.notEqual((await readCMakeToolchainEvidence(build,file)).compilers[0].sha256,first.compilers[0].sha256);
  await writeFile(file,JSON.stringify({...data,version:{major:2}}));
  await assert.rejects(readCMakeToolchainEvidence(build,file),/Incompatible/);
  await writeFile(file,JSON.stringify(data));await rm(root+'/compiler');
  await assert.rejects(readCMakeToolchainEvidence(build,file));
  await writeFile(root+'/outside.json',JSON.stringify(data));
  await assert.rejects(readCMakeToolchainEvidence(build,root+'/outside.json'),/outside/);
 }finally{await rm(root,{recursive:true,force:true});}
});

test('GNU component fingerprints detect subprocess and specs changes without claiming complete dependencies',async()=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch GNU evidence-'));
 try{
  const build=root+'/build',reply=build+'/.cmake/api/v1/reply';
  await mkdir(reply,{recursive:true});
  const roles=['cc1plus','collect2','as','ld','lto1','liblto_plugin.so'];
  for(const role of roles)await writeFile(root+'/'+role,role+'-v1');
  await writeFile(root+'/compiler','driver');
  const file=reply+'/toolchains.json';
  await writeFile(file,JSON.stringify({kind:'toolchains',version:{major:1},toolchains:[{language:'CXX',compiler:{path:root+'/compiler',id:'GNU',version:'13.3'}}]}));
  let specs='builtin specs v1';
  const probe=async(_compiler:string,arg:string)=>{
   if(arg==='-dumpspecs')return specs;
   const role=arg.split('=')[1];
   return role==='specs'?'specs':root+'/'+role;
  };
  const read=()=>readCMakeToolchainEvidence(build,file,probe);
  const before=await read();
  assert.equal(before.compilers[0].components?.length,6);
  assert.equal(before.dependenciesComplete,false);
  await writeFile(root+'/cc1plus','modified cc1plus');
  const after=await read();
  assert.notEqual(before.compilers[0].components?.[0].sha256,after.compilers[0].components?.[0].sha256);
  assert.equal(before.compilers[0].sha256,after.compilers[0].sha256);
  specs='builtin specs v2';
  assert.notEqual(before.compilers[0].specsSha256,(await read()).compilers[0].specsSha256);
  await rm(root+'/ld');
  await assert.rejects(read(),/ENOENT/);
  await writeFile(root+'/ld','linker');
  await assert.rejects(readCMakeToolchainEvidence(build,file,async()=>'/bad\nreply'),/Invalid GNU/);
  await assert.rejects(readCMakeToolchainEvidence(build,file,async()=> 'relative/path'),/cannot be resolved/);
 }finally{await rm(root,{recursive:true,force:true});}
});
