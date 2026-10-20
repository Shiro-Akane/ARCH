import test from 'node:test';
import assert from 'node:assert/strict';
import {parseNinjaDependencies,sameCompilerInputs} from '../host/ninjaDependencies.ts';
test('compiler dependency records retain external/space paths and reject stale, missing or truncated coverage',()=>{
 const record='CMakeFiles/target.dir/source file.cpp.o: #deps 2, deps mtime 123 (VALID)\n    /project/source file.cpp\n    /external/include/header.h\n\n';
 const rows=parseNinjaDependencies(record,'/project/build');
 assert.equal(rows[0].object,'/project/build/CMakeFiles/target.dir/source file.cpp.o');
 assert.deepEqual(rows[0].inputs,['/project/source file.cpp','/external/include/header.h']);
 for(const bad of ['',record.replace('VALID','STALE'),record.replace('#deps 2','#deps 3'),'    /orphan\n',record.replace(' (VALID)','')]){
  assert.throws(()=>parseNinjaDependencies(bad,'/project/build'));
 }
});

test('compiler stability rejects first-build unknown, changed content and changed dependency sets',()=>{
 const before={kind:'ninja-compiler-inputs' as const,objectCount:1,files:[{path:'/a',sha256:'abc',size:3}]};
 assert.equal(sameCompilerInputs(undefined,before),false);
 assert.equal(sameCompilerInputs(before,structuredClone(before)),true);
 assert.equal(sameCompilerInputs(before,{...before,files:[{path:'/a',sha256:'def',size:3}]}),false);
 assert.equal(sameCompilerInputs(before,{...before,files:[...before.files,{path:'/b',sha256:'xyz',size:3}]}),false);
 assert.equal(sameCompilerInputs(before,{...before,objectCount:2}),false);
});
