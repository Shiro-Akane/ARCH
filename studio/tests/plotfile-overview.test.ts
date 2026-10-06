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
async function withNative(run:(path:string,root:string)=>Promise<void>,nx=3,ny=2,blocks=2,overlap=false,
 storage:'fp64'|'fp32-field'|'integer-field'|'fp32-coordinate'='fp64'){
 await h5.ready;const root=await mkdtemp(join(tmpdir(),'arch-overview-')),path=join(root,'plot.h5');
 try{
  const cells=blocks*nx*ny,blockCells=nx*ny;
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
    if(overlap&&axis===1)lower[blockCells]=nx-.5;
    native.create_dataset({name:'x'+axis+'_lower',data:lower});
    native.create_dataset({name:'x'+axis+'_upper',data:Float64Array.from(lower,v=>axis===3?0:v+1)});
    native.create_dataset({name:'logical_x'+axis,data:Uint32Array.from({length:blocks},(_,i)=>axis===1?i:0)});
    grid.create_dataset({name:['x','y','z'][axis-1],data:storage==='fp32-coordinate'&&axis===1?
     Float32Array.from(lower,v=>v+.5):Float64Array.from(lower,v=>axis===3?0:v+.5)});
   }
   native.create_dataset({name:'cell_measure',data:new Float64Array(cells).fill(1)});
   for(const name of ['level','morton'])grid.create_dataset({name,data:Int32Array.from({length:blocks},(_,i)=>name==='level'?0:i)});
   const values=storage==='fp32-field'?Float32Array.from({length:cells},(_,i)=>i):
    storage==='integer-field'?Int32Array.from({length:cells},(_,i)=>i):Float64Array.from({length:cells},(_,i)=>i);
   f.create_group('Data').create_dataset({name:'DENS',data:values,shape:[blocks,ny,nx]});
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

test('native block outlines bind to raw level/logical identity and only emitted leaf bounds',async()=>{
 await withNative(async path=>{
  const r=await readPlotfileOverview(path,{field:'DENS',width:6,height:2}),leaf=r.overview!.nativeBlocks!;
  assert.equal(leaf.complete,true);assert.equal(leaf.totalBlocks,2);
  assert.deepEqual(leaf.blocks.map(b=>b.logicalKey),['0/0/0/0','0/1/0/0']);
  assert.deepEqual(leaf.blocks.map(b=>b.lower),[[0,0,0],[3,0,0]]);
  assert.deepEqual(leaf.blocks.map(b=>b.upper),[[3,2,0],[6,2,0]]);
  assert.deepEqual(leaf.blocks.map(b=>b.firstCellIndex),[0,6]);
  assert.deepEqual(leaf.blocks[0].cellShape,[3,2,1]);
  const request={field:'DENS',width:6,height:2},overview=r.overview!;
  for(const change of [
   {...leaf,complete:false}, {...leaf,totalBlocks:3}, {...leaf,identityScope:'global'},
   {...leaf,blocks:leaf.blocks.map((b,i)=>i===1?{...b,logicalKey:leaf.blocks[0].logicalKey}:b)},
   {...leaf,blocks:leaf.blocks.map((b,i)=>i===0?{...b,level:-1}:b)},
   {...leaf,blocks:leaf.blocks.map((b,i)=>i===0?{...b,upper:[7,2,0]}:b)},
   {...leaf,blocks:leaf.blocks.map((b,i)=>i===0?{...b,firstCellIndex:1}:b)},
  ])assert.equal(validOverview({...overview,nativeBlocks:change},request,12,2,[2,3],2),false);
  assert.equal(validOverview({...overview,nativeBlocks:{...leaf,blocks:leaf.blocks.map(b=>({...b,cellShape:[6,1,1]}))}},
   request,12,2,[2,3],2),false);
 });
});
test('limited native outlines do not truncate global field scanning or pretend to be complete',async()=>{
 await withNative(async path=>{
  const r=await readPlotfileOverview(path,{field:'DENS',width:32,height:1}),o=r.overview!,leaf=o.nativeBlocks!;
  assert.equal(o.scannedCells,129);assert.deepEqual(o.domain,{x:[0,129],y:[0,1]});
  assert.equal(leaf.totalBlocks,129);assert.equal(leaf.complete,false);assert.equal(leaf.blocks.length,128);
  assert.equal(leaf.blocks.at(-1)?.index,127);
  assert.equal(validOverview(o,{field:'DENS',width:32,height:1},129,2,[1,1],129),true);
  assert.equal(validOverview({...o,nativeBlocks:{...leaf,complete:true}},
   {field:'DENS',width:32,height:1},129,2,[1,1],129),false);
 },1,1,129);
});

import {copyPointRequest,nativeAxisContains} from '../src/host/plotfilePoint.ts';
import {readPlotfilePoint} from '../host/plotfileMetadata.ts';
import {readPlotfilePointIsolated} from '../host/isolatedPlotfileMetadata.ts';
import {readProjectPlotfilePoint} from '../host/projectPlotfileMetadata.ts';
import {validatePlotfilePoint} from '../src/host/plotfileAudit.ts';
test('native point boundary rules are explicit and requests cannot carry arbitrary execution',()=>{
 assert.equal(nativeAxisContains(1,0,1,2),false);
 assert.equal(nativeAxisContains(1,1,2,2),true);
 assert.equal(nativeAxisContains(2,1,2,2),true);
 for(const r of [null,{field:'DENS',point:[NaN]},{field:'DENS',point:[]},
  {field:'DENS',point:[1,2,3]},{field:'DENS',point:[0],env:{}}])assert.throws(()=>copyPointRequest(r));
 const r={field:'DENS',point:[1,2]},copy=copyPointRequest(r);r.point[0]=99;assert.deepEqual(copy.point,[1,2]);
});
test('exact native point resolves interior, shared block edges, global maximum and raw values',async()=>{
 await withNative(async path=>{
  for(const [point,index] of [[[0,0],0],[[2.5,.5],2],[[3,1],9],[[6,2],11]] as [number[],number][]){
   const result=await readPlotfilePoint(path,{field:'DENS',point});
   assert.deepEqual(result.payload?.linearIndices,[index]);assert.deepEqual(result.payload?.values,[index]);
   assert.equal(result.pointEvidence?.scannedCells,12);
  }
  await assert.rejects(readPlotfilePoint(path,{field:'DENS',point:[-1,0]}),/NO_NATIVE_CELL/);
  await assert.rejects(readPlotfilePoint(path,{field:'DENS',point:[0]}),/dimension/);
 });
});
test('point isolated/caller identity rejects wrong coordinates, stale digest and cancels before releasing ownership',async()=>{
 await withNative(async(path,root)=>{
  const point={field:'DENS',point:[3.5,1.5]},controller=new AbortController();
  const pending=readPlotfilePointIsolated(path,point,{signal:controller.signal});controller.abort();
  await assert.rejects(pending,e=>e instanceof PlotfileReadError&&e.code==='CANCELLED');
  const result=await readPlotfilePointIsolated(path,point);
  const body={projectId:'point',relativePath:'plot.h5',pointQuery:point,expectedFileSha256:result.file.sha256};
  const response=await readProjectPlotfilePoint(root,'point',body);
  assert.deepEqual(validatePlotfilePoint(response,'point','plot.h5',point,result.file.sha256).audit.payload?.values,[9]);
  assert.throws(()=>validatePlotfilePoint(response,'point','plot.h5',{field:'DENS',point:[4.5,1.5]},result.file.sha256),/point/);
  await assert.rejects(readProjectPlotfilePoint(root,'point',{...body,expectedFileSha256:'0'.repeat(64)}),/SHA-256/);
  await assert.rejects(readProjectPlotfilePoint(root,'point',{...body,command:'sh'}));
  assert.throws(()=>validatePlotfilePoint({...response,result:{...response.result,pointEvidence:{...response.result.pointEvidence,matchCount:2}}},
   'point','plot.h5',point,result.file.sha256),/point/);
 });
});

test('overlapping stored cells fail the exact point query instead of guessing a level or nearest value',async()=>{
 await withNative(async path=>{
  await assert.rejects(readPlotfilePoint(path,{field:'DENS',point:[2.75,.5]}),/AMBIGUOUS_NATIVE_CELL/);
 },3,2,2,true);
});

test('viewport request deeply copies physical ranges and rejects invalid or arbitrary properties',()=>{
 const r={field:'DENS',width:2,height:2,viewport:{x:[1,2],y:[3,4]}},copy=copyOverviewRequest(r);
 r.viewport.x[0]=99;assert.deepEqual(copy.viewport,{x:[1,2],y:[3,4]});
 for(const viewport of [undefined,null,{x:[1,1],y:[0,1]},{x:[0,Infinity],y:[0,1]},
  {x:[0,1],y:[0,1],command:'sh'},{x:[0,1],y:[0,1,2]}])
  assert.throws(()=>copyOverviewRequest({field:'DENS',width:2,height:2,viewport}));
});
test('viewport LOD clips raw native contributions but preserves complete stored domain and block identity',async()=>{
 await withNative(async path=>{
  const request={field:'DENS',width:2,height:2,viewport:{x:[2,4] as [number,number],y:[.25,1.75] as [number,number]}};
  const r=await readPlotfileOverview(path,request),o=r.overview!;
  assert.deepEqual(o.domain,request.viewport);assert.deepEqual(o.globalDomain,{x:[0,6],y:[0,2]});
  assert.deepEqual(o.values,[2,6,5,9]);assert.deepEqual(o.representativeIndices,[2,6,5,9]);
  assert.equal(o.scannedCells,12);assert.equal(o.nativeBlocks?.blocks.length,2);
  assert.deepEqual(o.nativeBlocks?.blocks[0].lower,[0,0,0]);
  assert.equal(validOverview(o,request,12,2,[2,3],2),true);
  assert.equal(validOverview({...o,domain:{x:[0,6],y:[0,2]}},request,12,2,[2,3],2),false);
  const outside=await readPlotfileOverview(path,{...request,viewport:{x:[20,21],y:[0,2]}});
  assert.deepEqual(outside.overview?.values,[null,null,null,null]);
  assert.equal(outside.overview?.scannedCells,12);
 });
});

test('candidate native fields and center coordinates must be FP64 on every read path',async()=>{
 for(const storage of ['fp32-field','integer-field','fp32-coordinate'] as const)
  await withNative(async path=>{
   const before=await readFile(path);
   const {inspectPlotfileMetadata,readPlotfileFieldSlice}=await import('../host/plotfileMetadata.ts');
   await assert.rejects(inspectPlotfileMetadata(path),/Candidate native .* requires FP64/);
   await assert.rejects(readPlotfileFieldSlice(path,{field:'DENS',block:0,start:[0,0],count:[1,1]}),
    /Candidate native .* requires FP64/);
   await assert.rejects(readPlotfileOverview(path,{field:'DENS',width:6,height:2}),
    /Candidate native .* requires FP64/);
   await assert.rejects(readPlotfilePoint(path,{field:'DENS',point:[.5,.5]}),
    /Candidate native .* requires FP64/);
   await assert.rejects(inspectPlotfileMetadataIsolated(path),/Candidate native .* requires FP64/);
   assert.deepEqual(await readFile(path),before);
  },3,2,2,false,storage);
});
