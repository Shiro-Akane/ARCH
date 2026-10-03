import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,symlink,rm} from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {canonicalOutputDirectory,reserveRunOutputs} from '../host/runOutput.ts';
test('canonical outputs resolve aliases and missing suffixes without creating output',async()=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'ARCH outputs 中文 '));
 try{
  await mkdir(root+'/actual');await symlink(root+'/actual',root+'/alias');
  assert.equal(await canonicalOutputDirectory(root+'/alias/new/nested'),root+'/actual/new/nested');
  const a=await reserveRunOutputs([{path:root+'/alias/new',canonicalPath:root+'/actual/new'},
   {path:root+'/actual/new',canonicalPath:root+'/actual/new'}]);
  try{await assert.rejects(reserveRunOutputs([{path:root+'/actual/new',canonicalPath:root+'/actual/new'}]),/reserved by another/);}
  finally{await Promise.all(a.map(f=>f.close()));}
  const b=await reserveRunOutputs([{path:root+'/actual/new',canonicalPath:root+'/actual/new'}]);
  await Promise.all(b.map(f=>f.close()));
  await assert.rejects(import('node:fs/promises').then(fs=>fs.stat(root+'/actual/new')),/ENOENT/);
 }finally{await rm(root,{recursive:true,force:true});}
});
test('changed aliases and dangling links are refused before reserving output',async()=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'ARCH outputs '));
 try{
  await mkdir(root+'/a');await mkdir(root+'/b');await symlink(root+'/a',root+'/alias');
  const canonicalPath=await canonicalOutputDirectory(root+'/alias/new');
  await rm(root+'/alias');await symlink(root+'/b',root+'/alias');
  await assert.rejects(reserveRunOutputs([{path:root+'/alias/new',canonicalPath}]),/changed after confirmation/);
  await symlink(root+'/missing',root+'/dangling');
  await assert.rejects(canonicalOutputDirectory(root+'/dangling/new'),/unresolved symbolic link/);
 }finally{await rm(root,{recursive:true,force:true});}
});
test('failed multi-output reservation releases earlier claims',async()=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'ARCH outputs '));
 const item=(name:string)=>({path:root+'/'+name,canonicalPath:root+'/'+name});
 let held:Awaited<ReturnType<typeof reserveRunOutputs>>=[];
 try{
  held=await reserveRunOutputs([item('z')]);
  await assert.rejects(reserveRunOutputs([item('a'),item('z')]),/reserved by another/);
  const free=await reserveRunOutputs([item('a')]);await Promise.all(free.map(f=>f.close()));
 }finally{await Promise.all(held.map(f=>f.close()));await rm(root,{recursive:true,force:true});}
});
