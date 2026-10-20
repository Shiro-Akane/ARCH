import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,rm,symlink,unlink} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {parseLinkDependencies,fingerprintLinkDependencies,changedLinkInputs} from '../host/linkDependencies.ts';
test('link depfile keeps escaped paths and rejects wrong output, variables and extra rules',()=>{
 const input='bin/ARCH: a\\ b.o /lib/libx.so \\\n /lib/hash\\#name.o dollar$$.o\n\na\\ b.o:\n/lib/libx.so:\n';
 assert.deepEqual(parseLinkDependencies(input,'/build','/build/bin/ARCH'),['/build/a b.o','/build/dollar$.o','/lib/hash#name.o','/lib/libx.so']);
 for(const bad of ['',input.replace('bin/ARCH:','other:'),'bin/ARCH: $(INPUT)\n','bin/ARCH: a.o\nother: b.o\n','bin/ARCH: a.o\\','bin/ARCH: *.o\n'])
  assert.throws(()=>parseLinkDependencies(bad,'/build','/build/bin/ARCH'));
});
test('link identities detect external library change and symlink retarget; missing inputs remain explicit',async()=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'studio-link-'));
 try{
  await mkdir(root+'/build');
  await writeFile(root+'/one.so','library A');await writeFile(root+'/two.so','library A');
  await symlink(root+'/one.so',root+'/selected.so');
  await writeFile(root+'/build/object.o','object');
  await writeFile(root+'/build/ARCH.link.d','bin/ARCH: object.o '+root+'/selected.so '+root+'/missing.ltrans.o\n');
  const first=await fingerprintLinkDependencies(root+'/build','ARCH.link.d',root+'/build/bin/ARCH');
  assert.equal(first.files.length,2);assert.deepEqual(first.unavailable,[{path:root+'/missing.ltrans.o',reason:'missing'}]);
  await writeFile(root+'/one.so','library B');
  const changed=await fingerprintLinkDependencies(root+'/build','ARCH.link.d',root+'/build/bin/ARCH');
  assert.deepEqual(changedLinkInputs(first,changed),[root+'/selected.so']);
  await unlink(root+'/selected.so');await symlink(root+'/two.so',root+'/selected.so');
  const retarget=await fingerprintLinkDependencies(root+'/build','ARCH.link.d',root+'/build/bin/ARCH');
  assert.deepEqual(changedLinkInputs(first,retarget),[root+'/selected.so']);
  await assert.rejects(fingerprintLinkDependencies(root+'/build','../escape','/wrong'));
 }finally{await rm(root,{recursive:true,force:true});}
});
