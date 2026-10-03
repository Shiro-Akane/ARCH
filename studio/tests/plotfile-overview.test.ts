import {test} from 'node:test';
import assert from 'node:assert/strict';
import {copyOverviewRequest,createOverview,validOverview} from '../src/host/plotfileOverview.ts';
import {readPlotfileOverview} from '../host/plotfileMetadata.ts';

test('overview requests reject execution knobs and bound output dimensions; copies detach mutable requests',()=>{
 const r={field:'DENS',width:32,height:24},copy=copyOverviewRequest(r);r.width=1;assert.equal(copy.width,32);
 for(const value of [null,[],{field:'DENS',width:33,height:1},{field:'DENS',width:1,height:0},
  {field:'DENS',width:NaN,height:1},{field:'DENS',width:1,height:1,command:'run'}])
  assert.throws(()=>copyOverviewRequest(value));
});
test('two AMR leaf widths contribute coordinate overlap means; representative stays a raw native index',()=>{
 const acc=createOverview({field:'DENS',width:2,height:1},1,{x:[0,2],y:[0,1]});
 acc.add(0,[0,0],[.5,1],1);acc.add(1,[.5,0],[1,1],3);acc.add(2,[1,0],[2,1],8);
 const o=acc.finish();assert.deepEqual(o.values,[2,8]);assert.deepEqual(o.representativeIndices,[0,2]);
 assert.equal(o.scannedCells,3);assert.ok(validOverview(o,{field:'DENS',width:2,height:1},3,1));
 assert.ok(!validOverview({...o,values:[2]}, {field:'DENS',width:2,height:1},3,1));
 assert.ok(!validOverview({...o,representativeIndices:[3,2]}, {field:'DENS',width:2,height:1},3,1));
 assert.throws(()=>createOverview({field:'DENS',width:2,height:2},1,{x:[0,2],y:[0,1]}));
});
test('non-square 2D overview keeps x1-fastest and does not conflate coordinate area with scientific measure',()=>{
 const acc=createOverview({field:'DENS',width:3,height:2},2,{x:[0,3],y:[10,12]});
 for(let j=0;j<2;j++)for(let i=0;i<3;i++)acc.add(j*3+i,[i,10+j],[i+1,11+j],j*10+i);
 assert.deepEqual(acc.finish().values,[0,1,2,10,11,12]);
 assert.deepEqual(acc.finish().representativeIndices,[0,1,2,3,4,5]);
});
test('nonfinite overlap masks a display pixel; empty pixels stay null; native storage is not repaired',()=>{
 const acc=createOverview({field:'PRES',width:3,height:1},1,{x:[0,3],y:[0,1]});
 acc.add(0,[0,0],[.5,1],1);acc.add(1,[.5,0],[1,1],NaN);acc.add(2,[1,0],[2,1],-3);
 const o=acc.finish();assert.deepEqual(o.values,[null,-3,null]);assert.equal(o.nonfiniteCells,1);
 assert.ok(o.diagnostics.includes('NONFINITE_VALUES_MASKED_EXPLICITLY'));
});
test('legacy files cannot acquire guessed native bounds through the overview endpoint',async()=>{
 await assert.rejects(readPlotfileOverview(new URL('./fixtures/sod-1d.h5',import.meta.url).pathname,
 {field:'DENS',width:4,height:1}),/native Cartesian/);
});

import h5 from 'h5wasm/node';
import {mkdtemp,rm,readFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {readPlotfileOverviewIsolated,inspectPlotfileMetadataIsolated,PlotfileReadError} from '../host/isolatedPlotfileMetadata.ts';
import {readProjectPlotfileOverview} from '../host/projectPlotfileMetadata.ts';
import {validatePlotfileOverview} from '../src/host/plotfileAudit.ts';
async function withNative(run:(path:string,root:string)=>Promise<void>,nx=3,ny=2){
 await h5.ready;const root=await mkdtemp(join(tmpdir(),'arch-overview-')),path=join(root,'plot.h5');
 try{
  const cells=2*nx*ny,blockCells=nx*ny;
  const f=new h5.File(path,'w');
  try{
   for(const [k,v] of Object.entries({time:0,dim:2,geometry:'cartesian',
    plot_publication_version:'candidate-1',plot_publication_state:'complete',
    plot_publication_method:'checked-close-atomic-replace',plot_storage_order:'x1-fastest'}))f.create_attribute(k,v);
   const grid=f.create_group('Grid'),native=f.create_group('NativeGrid');
   for(const [k,v] of Object.entries({version:'candidate-cartesian-1',centering:'cell',ghost_cells:0,
    block_kind:'active-leaf',center_basis:'cartesian',measure_source:'GridMetrics::CellVolume',
    measure_convention:'active-coordinate-product; inactive-measures-omitted',measure_unit:'unknown',
    logical_identity:'file-local level/logical_x1/logical_x2/logical_x3'}))native.create_attribute(k,v);
   for(let axis=1;axis<=3;axis++){
    const lower=Float64Array.from({length:cells},(_,i)=>axis===1?Math.floor(i/blockCells)*nx+i%nx:axis===2?Math.floor(i%blockCells/nx):0);
    native.create_dataset({name:'x'+axis+'_lower',data:lower});
    native.create_dataset({name:'x'+axis+'_upper',data:Float64Array.from(lower,v=>axis===3?0:v+1)});
    native.create_dataset({name:'logical_x'+axis,data:new Uint32Array(axis===1?[0,1]:[0,0])});
    grid.create_dataset({name:['x','y','z'][axis-1],data:Float64Array.from(lower,v=>axis===3?0:v+.5)});
   }
   native.create_dataset({name:'cell_measure',data:new Float64Array(cells).fill(1)});
   for(const name of ['level','morton'])grid.create_dataset({name,data:new Int32Array([0,0])});
   f.create_group('Data').create_dataset({name:'DENS',data:Float64Array.from({length:cells},(_,i)=>i),shape:[2,ny,nx]});
  }finally{f.close();}
  await run(path,root);
 }finally{await rm(root,{recursive:true,force:true});}
}
test('real HDF5 cross-block overview streams geometry/field without full dataset getters',async()=>{
 await withNative(async path=>{
  const before=await readFile(path),descriptor=Object.getOwnPropertyDescriptor(h5.Dataset.prototype,'value')!;
  Object.defineProperty(h5.Dataset.prototype,'value',{get(){throw Error('Full dataset getter forbidden');},configurable:true});
  try{
   const r=await readPlotfileOverview(path,{field:'DENS',width:6,height:2});
   assert.deepEqual(r.overview?.domain,{x:[0,6],y:[0,2]});
   assert.deepEqual(r.overview?.values,[0,1,2,6,7,8,3,4,5,9,10,11]);
   assert.equal(r.overview?.scannedCells,12);
   assert.deepEqual(await readFile(path),before);
  }finally{Object.defineProperty(h5.Dataset.prototype,'value',descriptor);}
 });
});
test('overview isolation shares cancellation/capacity, releases on exit, and checks project/hash/body identity',async()=>{
 await withNative(async(path,root)=>{
  const request={field:'DENS',width:6,height:2},controller=new AbortController();
  const pending=readPlotfileOverviewIsolated(path,request,{signal:controller.signal});
  await assert.rejects(inspectPlotfileMetadataIsolated(path),e=>e instanceof PlotfileReadError&&e.code==='BUSY');
  controller.abort();await assert.rejects(pending,e=>e instanceof PlotfileReadError&&e.code==='CANCELLED');
  const read=await readPlotfileOverviewIsolated(path,request);
  const body={projectId:'test',relativePath:'plot.h5',expectedFileSha256:read.file.sha256,overview:request};
  const scoped=await readProjectPlotfileOverview(root,'test',body);
  const valid=validatePlotfileOverview(scoped,'test','plot.h5',request,read.file.sha256);
  assert.equal(valid.audit.overview?.scannedCells,12);
  await assert.rejects(readProjectPlotfileOverview(root,'test',{...body,expectedFileSha256:'0'.repeat(64)}),/SHA-256/);
  for(const bad of [{...body,command:'sh'},{...body,projectId:'other'},{...body,relativePath:'../plot.h5'},
   {...body,overview:{...request,env:{}}}])await assert.rejects(readProjectPlotfileOverview(root,'test',bad));
  assert.throws(()=>validatePlotfileOverview({...scoped,result:{...scoped.result,overview:{...scoped.result.overview,scannedCells:0}}},
   'test','plot.h5',request,read.file.sha256),/overview/);
 });
});

test('overview batching covers wide rows and partial final row slabs without changing native order',async()=>{
 await withNative(async path=>{
  const r=await readPlotfileOverview(path,{field:'DENS',width:6,height:2});
  assert.equal(r.overview?.scannedCells,2052);
  assert.deepEqual(r.overview?.values,[85,256,427,1111,1282,1453,598,769,940,1624,1795,1966]);
 },513,2);
 await withNative(async path=>{
  const r=await readPlotfileOverview(path,{field:'DENS',width:2,height:20});
  assert.equal(r.overview?.scannedCells,1280);
  assert.deepEqual(r.overview?.values,Array.from({length:20},(_,j)=>[j*32+15.5,640+j*32+15.5]).flat());
 },32,20);
});
