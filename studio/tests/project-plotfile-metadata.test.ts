import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,rm,copyFile,symlink,mkdir} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {readProjectPlotfileMetadata} from '../host/projectPlotfileMetadata.ts';
import {inspectPlotfileMetadata} from '../host/plotfileMetadata.ts';

const fixture=new URL('./fixtures/sod-1d.h5',import.meta.url).pathname;
test('project metadata carries only observed project/file identity and preserves unknown scientific identity',async()=>{
 const root=await mkdtemp(join(tmpdir(),'arch-project-plt-'));
 try{
  await copyFile(fixture,join(root,'result.h5'));
  const result=await readProjectPlotfileMetadata(root,'session',{projectId:'session',relativePath:'result.h5'});
  assert.equal(result.projectId,'session');assert.equal(result.relativePath,'result.h5');
  assert.equal(result.metadata.scientificIdentity.case,null);
  assert.equal(result.metadata.renderEligible,false);
  assert.equal(result.metadata.completion.state,'unknown');
 }finally{await rm(root,{recursive:true,force:true});}
});
test('project identity, arbitrary execution fields, traversal and all symlink selections reject',async()=>{
 const root=await mkdtemp(join(tmpdir(),'arch-project-plt-'));
 try{
  await copyFile(fixture,join(root,'result.h5'));
  for(const request of [
   {projectId:'other',relativePath:'result.h5'},
   {projectId:'session',relativePath:fixture},
   {projectId:'session',relativePath:'../result.h5'},
   {projectId:'session',relativePath:'result.h5',env:{}},
   {projectId:'session',relativePath:'result.h5',command:'anything'},
  ])await assert.rejects(readProjectPlotfileMetadata(root,'session',request));
  await symlink(join(root,'result.h5'),join(root,'link.h5'));
  await mkdir(join(root,'actual'));await copyFile(fixture,join(root,'actual/result.h5'));
  await symlink(join(root,'actual'),join(root,'linked-directory'));
  for(const relativePath of ['link.h5','linked-directory/result.h5'])
   await assert.rejects(readProjectPlotfileMetadata(root,'session',{projectId:'session',relativePath}),/Symlinks/);
  // The low-level primitive independently refuses a symlink parent, before parsing.
  await assert.rejects(inspectPlotfileMetadata(join(root,'linked-directory/result.h5')),/path identity/);
 }finally{await rm(root,{recursive:true,force:true});}
});
