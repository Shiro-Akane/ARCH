import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,rm,readFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import h5 from 'h5wasm/node';
import {readPlotfileFieldSlice} from '../host/plotfileMetadata.ts';

await h5.ready;
async function fixture(run:(path:string)=>Promise<void>,shape=[2,3,5],change?:(file:InstanceType<typeof h5.File>)=>void){
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
   change?.(f);
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


function nativeFixture(f:InstanceType<typeof h5.File>,invalid?:string){
 for(const [name,value] of Object.entries({
  plot_publication_version:'candidate-1',plot_publication_state:invalid==='partial'?'writing':'complete',
  plot_publication_method:'checked-close-atomic-replace',plot_storage_order:'x1-fastest',
 }))f.create_attribute(name,value);
 const g=f.create_group('NativeGrid');
 for(const [name,value] of Object.entries({
  version:invalid==='version'?'future-unknown':'candidate-cartesian-1',centering:'cell',
  block_kind:'active-leaf',center_basis:'cartesian',measure_source:'GridMetrics::CellVolume',
  measure_convention:'active-coordinate-product; inactive-measures-omitted',measure_unit:'unknown',
  logical_identity:'file-local level/logical_x1/logical_x2/logical_x3',
 }))g.create_attribute(name,value);
 g.create_attribute('ghost_cells',0);
 for(let axis=1;axis<=3;axis++){
  const lower=Float64Array.from({length:30},(_,i)=>axis===3?0:i+100*(axis-1));
  const upper=Float64Array.from(lower,n=>axis===3?0:n+.5);
  if(invalid==='bounds'&&axis===1)upper[21]=lower[21];
  g.create_dataset({name:'x'+axis+'_lower',data:lower});
  g.create_dataset({name:'x'+axis+'_upper',data:upper});
  g.create_dataset({name:'logical_x'+axis,data:new Uint32Array([axis,axis+2])});
 }
 const measure=new Float64Array(30).fill(.25);
 if(invalid==='measure')measure[21]=NaN;
 g.create_dataset({name:'cell_measure',data:measure});
}
test('candidate native query reads bounded stored bounds/measure/logical identity, preserves file and raw field',async()=>{
 await fixture(async path=>{
  const before=await readFile(path);
  const descriptor=Object.getOwnPropertyDescriptor(h5.Dataset.prototype,'value')!;
  Object.defineProperty(h5.Dataset.prototype,'value',{get(){throw Error('Full array read forbidden');},configurable:true});
  try{
   const r=await readPlotfileFieldSlice(path,{field:'DENS',block:1,start:[1,1],count:[2,3]});
   assert.deepEqual(r.payload?.linearIndices,[21,22,23,26,27,28]);
   assert.deepEqual(r.payload?.nativeCells?.lower.x1,[21,22,23,26,27,28]);
   assert.deepEqual(r.payload?.nativeCells?.upper.x2,[121.5,122.5,123.5,126.5,127.5,128.5]);
   assert.deepEqual(r.payload?.nativeCells?.cellMeasure,new Array(6).fill(.25));
   assert.deepEqual(r.payload?.nativeCells?.logicalCoordinates,[3,4,5]);
   assert.equal(r.payload?.nativeCells?.level,1);
   assert.equal(r.payload?.nativeCells?.logicalKey,'1/3/4/5');
   assert.equal(r.payload?.nativeCells?.identityScope,'file-local');
   assert.equal(r.payload?.nativeCells?.measureUnit,null);
   assert.equal(r.completion.state,'unknown');assert.equal(r.renderEligible,false);
   assert.equal(r.scientificIdentity.case,null);
   assert.equal(r.candidateNativeGrid?.version,'candidate-cartesian-1');
   assert.deepEqual(await readFile(path),before);
  }finally{Object.defineProperty(h5.Dataset.prototype,'value',descriptor);}
 },[2,3,5],nativeFixture);
});
test('unknown native version, partial publication, invalid bounds/measure reject and legacy recovers',async()=>{
 for(const invalid of ['version','partial','bounds','measure'])
  await fixture(async path=>{
   await assert.rejects(readPlotfileFieldSlice(path,{field:'DENS',block:1,start:[1,1],count:[1,1]}),
    /candidate native|native metadata|native publication|native cell geometry|native bounds/i);
  },[2,3,5],f=>nativeFixture(f,invalid));
 await fixture(async path=>{
  const r=await readPlotfileFieldSlice(path,{field:'DENS',block:0,start:[0,0],count:[1,1]});
  assert.equal(r.payload?.nativeCells,null);assert.equal(r.candidateNativeGrid,null);
 });
});
