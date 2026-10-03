import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,rm,readFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import h5 from 'h5wasm/node';
import {readPlotfileFieldSlice} from '../host/plotfileMetadata.ts';

await h5.ready;
async function fixture(run:(path:string)=>Promise<void>,shape=[2,3,5]){
 const dir=await mkdtemp(join(tmpdir(),'arch-plt-slice-')),path=join(dir,'fixture.h5');
 try{
  const f=new h5.File(path,'w');
  try{
   f.create_attribute('time',0);f.create_attribute('dim',shape.length-1);f.create_attribute('geometry','cartesian');
   const cells=shape.reduce((a,b)=>a*b,1),grid=f.create_group('Grid');
   for(const axis of ['x','y','z'])grid.create_dataset({name:axis,data:Float64Array.from({length:cells},(_,i)=>i+(axis==='x'?0:axis==='y'?100:200))});
   for(const name of ['level','morton'])grid.create_dataset({name,data:Int32Array.from({length:shape[0]},(_,i)=>i)});
   const values=Float64Array.from({length:cells},(_,i)=>i);values[cells-1]=NaN;
   f.create_group('Data').create_dataset({name:'DENS',data:values,shape});
  }finally{f.close();}
  await run(path);
 }finally{await rm(dir,{recursive:true,force:true});}
}
test('non-square block slice preserves raw indices and aligned Cartesian centers without full dataset reads',async()=>{
 await fixture(async path=>{
  const before=await readFile(path);
  const descriptor=Object.getOwnPropertyDescriptor(h5.Dataset.prototype,'value')!;
  Object.defineProperty(h5.Dataset.prototype,'value',{get(){throw Error('Full array read forbidden');},configurable:true});
  try{
   const r=await readPlotfileFieldSlice(path,{field:'DENS',block:1,start:[1,1],count:[2,3]});
   assert.deepEqual(r.payload?.linearIndices,[21,22,23,26,27,28]);
   assert.deepEqual(r.payload?.values,[21,22,23,26,27,28]);
   assert.deepEqual(r.payload?.coordinates.y,[121,122,123,126,127,128]);
   assert.deepEqual(r.payload?.shape,[2,3]);
   assert.equal(r.renderEligible,false);assert.equal(r.completion.state,'unknown');
   assert.equal(r.payload?.unit,null);
   assert.deepEqual(await readFile(path),before);
  }finally{Object.defineProperty(h5.Dataset.prototype,'value',descriptor);}
 });
});
test('nonfinite samples are explicit raw values, not silently zero or null',async()=>{
 await fixture(async path=>{
  const r=await readPlotfileFieldSlice(path,{field:'DENS',block:1,start:[2,4],count:[1,1]});
  assert.deepEqual(r.payload?.values,['NaN']);assert.deepEqual(r.payload?.diagnostics,['NONFINITE_RAW_VALUES']);
  assert.match(JSON.stringify(r),/"NaN"/);
 });
});
test('unknown fields, out-of-range and malformed slices fail and subsequent reads recover',async()=>{
 await fixture(async path=>{
  for(const request of [
   {field:'PRES',block:0,start:[0,0],count:[1,1]},
   {field:'DENS',block:2,start:[0,0],count:[1,1]},
   {field:'DENS',block:0,start:[0,4],count:[1,2]},
   {field:'DENS',block:0,start:[0,0],count:[1,0]},
   {field:'DENS',block:0,start:[0],count:[1]},
   {field:'DENS',block:0,start:[NaN,0],count:[1,1]},
  ])await assert.rejects(readPlotfileFieldSlice(path,request));
  assert.deepEqual((await readPlotfileFieldSlice(path,{field:'DENS',block:0,start:[0,0],count:[1,1]})).payload?.values,[0]);
 });
});

test('3D z/y/x hyperslab uses x1-fastest global indices',async()=>{
 await fixture(async path=>{
  const r=await readPlotfileFieldSlice(path,{field:'DENS',block:1,start:[1,2,3],count:[2,1,2]});
  assert.deepEqual(r.payload?.shape,[2,1,2]);
  assert.deepEqual(r.payload?.linearIndices,[93,94,113,114]);
  assert.deepEqual(r.payload?.values,[93,94,113,114]);
  assert.deepEqual(r.payload?.coordinates.x,[93,94,113,114]);
 },[2,3,4,5]);
});
test('512-sample budget rejects before hyperslab read and valid boundary recovers',async()=>{
 await fixture(async path=>{
  const original=h5.Dataset.prototype.slice;let reads=0;
  h5.Dataset.prototype.slice=function(ranges){reads++;return original.call(this,ranges);};
  try{
   await assert.rejects(readPlotfileFieldSlice(path,{field:'DENS',block:0,start:[0,0,0],count:[9,8,8]}),/512-sample/);
   assert.equal(reads,0);
   const r=await readPlotfileFieldSlice(path,{field:'DENS',block:0,start:[0,0,0],count:[8,8,8]});
   assert.equal(r.payload?.values.length,512);assert.ok(reads>0);
  }finally{h5.Dataset.prototype.slice=original;}
 },[1,9,9,9]);
});
