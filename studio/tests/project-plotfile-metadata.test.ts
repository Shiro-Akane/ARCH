import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,rm,copyFile,symlink,mkdir} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {readProjectPlotfileMetadata,readProjectPlotfileFieldSlice} from '../host/projectPlotfileMetadata.ts';
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

test('project slice requires matching selected file hash and rejects stale identity',async()=>{
 const root=await mkdtemp(join(tmpdir(),'arch-project-slice-'));
 try{
  await copyFile(fixture,join(root,'result.h5'));
  const metadata=await readProjectPlotfileMetadata(root,'session',{projectId:'session',relativePath:'result.h5'});
  const request={projectId:'session',relativePath:'result.h5',expectedFileSha256:metadata.metadata.file.sha256,slice:{field:'DENS',block:0,start:[0],count:[8]}};
  const result=await readProjectPlotfileFieldSlice(root,'session',request);
  assert.equal(result.result.file.sha256,request.expectedFileSha256);assert.equal(result.result.payload?.values.length,8);
  assert.equal(result.result.renderEligible,false);assert.equal(result.result.completion.state,'unknown');
  await assert.rejects(readProjectPlotfileFieldSlice(root,'session',{...request,expectedFileSha256:'0'.repeat(64)}),/SHA-256/);
  for(const bad of [
   {...request,expectedFileSha256:null}, {...request,projectId:'other'},
   {...request,relativePath:'../result.h5'}, {...request,relativePath:fixture},
   {...request,command:'anything'}, {...request,slice:{...request.slice,env:{}}},
  ])await assert.rejects(readProjectPlotfileFieldSlice(root,'session',bad));
  await symlink(join(root,'result.h5'),join(root,'link.h5'));
  await assert.rejects(readProjectPlotfileFieldSlice(root,'session',{...request,relativePath:'link.h5'}),/Symlinks/);
  assert.equal((await readProjectPlotfileFieldSlice(root,'session',request)).result.payload?.values.length,8);
 }finally{await rm(root,{recursive:true,force:true});}
});
test('caller mutation cannot replace copied project path or slice selection during asynchronous read',async()=>{
 const root=await mkdtemp(join(tmpdir(),'arch-project-slice-'));
 try{
  await copyFile(fixture,join(root,'result.h5'));
  const metadata=await readProjectPlotfileMetadata(root,'session',{projectId:'session',relativePath:'result.h5'});
  const request={projectId:'session',relativePath:'result.h5',expectedFileSha256:metadata.metadata.file.sha256,slice:{field:'DENS',block:0,start:[0],count:[8]}};
  const pending=readProjectPlotfileFieldSlice(root,'session',request);
  request.relativePath='../other.h5';request.slice.start[0]=100000;
  const result=await pending;assert.equal(result.relativePath,'result.h5');
  assert.deepEqual(result.result.payload?.start,[0]);assert.equal(result.result.payload?.values.length,8);
 }finally{await rm(root,{recursive:true,force:true});}
});
