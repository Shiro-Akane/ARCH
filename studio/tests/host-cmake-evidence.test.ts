import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,rm,readdir} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {readCMakeConfigurationEvidence} from '../host/cmakeEvidence.ts';
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
 }finally{await rm(root,{recursive:true,force:true});}
});
