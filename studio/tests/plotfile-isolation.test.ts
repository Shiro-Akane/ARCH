import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import h5 from 'h5wasm/node';
import {inspectPlotfileMetadataIsolated,PlotfileReadError} from '../host/isolatedPlotfileMetadata.ts';

const fixture=new URL('./fixtures/sod-1d.h5',import.meta.url).pathname;
const hasCode=(code:string)=>(error:unknown)=>error instanceof PlotfileReadError&&error.code===code;

test('isolated actual metadata stays read-only and truthful',async()=>{
 const before=await readFile(fixture);
 const result=await inspectPlotfileMetadataIsolated(fixture);
 assert.equal(result.dimension,1);assert.equal(result.time,.15);
 assert.equal(result.renderEligible,false);assert.equal(result.completion.state,'unknown');
 assert.deepEqual(await readFile(fixture),before);
});
test('pre-cancel does not occupy a worker; malformed input recovers',async()=>{
 const controller=new AbortController();controller.abort();
 await assert.rejects(inspectPlotfileMetadataIsolated(fixture,{signal:controller.signal}),hasCode('CANCELLED'));
 await assert.rejects(inspectPlotfileMetadataIsolated('/definitely/missing/ARCH-plot.h5'),hasCode('WORKER_FAILED'));
 assert.equal((await inspectPlotfileMetadataIsolated(fixture)).dimension,1);
});
test('timeout and in-flight cancellation reap worker before releasing capacity',async()=>{
 await assert.rejects(inspectPlotfileMetadataIsolated(fixture,{timeoutMs:1}),hasCode('TIMEOUT'));
 const controller=new AbortController();
 const pending=inspectPlotfileMetadataIsolated(fixture,{signal:controller.signal});
 await assert.rejects(inspectPlotfileMetadataIsolated(fixture),hasCode('BUSY'));
 controller.abort();
 await assert.rejects(pending,hasCode('CANCELLED'));
 assert.equal((await inspectPlotfileMetadataIsolated(fixture)).renderEligible,false);
});
test('invalid Host timeout is rejected without acquiring capacity',async()=>{
 for(const timeoutMs of [0,-1,15_001,NaN,1.5])
  await assert.rejects(inspectPlotfileMetadataIsolated(fixture,{timeoutMs}),RangeError);
 assert.equal((await inspectPlotfileMetadataIsolated(fixture)).dimension,1);
});

test('oversized real HDF5 metadata response terminates and releases worker capacity',async()=>{
 await h5.ready;
 const dir=await mkdtemp(join(tmpdir(),'arch-plt-isolation-'));
 try{
  const path=join(dir,'large-headers.h5');
  const file=new h5.File(path,'w');
  try{
   file.create_attribute('time',0);file.create_attribute('dim',1);file.create_attribute('geometry','cartesian');
   const grid=file.create_group('Grid');
   for(const name of ['x','y','z','level','morton'])grid.create_dataset({name,data:new Float64Array(1)});
   const data=file.create_group('Data');
   for(let i=0;i<80;i++)data.create_dataset({name:'FIELD_'+i+'_'+('a'.repeat(1000)),data:new Float64Array(1),shape:[1,1]});
  }finally{file.close();}
  await assert.rejects(inspectPlotfileMetadataIsolated(path),hasCode('OUTPUT_LIMIT'));
  assert.equal((await inspectPlotfileMetadataIsolated(fixture)).dimension,1);
 }finally{await rm(dir,{recursive:true,force:true});}
});
