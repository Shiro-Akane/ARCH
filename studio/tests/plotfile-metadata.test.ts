import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,rm,writeFile,symlink,readFile,open} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import h5 from 'h5wasm/node';
import {inspectPlotfileMetadata} from '../host/plotfileMetadata.ts';

await h5.ready;
async function fixture(run: (path: string)=>Promise<void>, change?: (file: InstanceType<typeof h5.File>)=>void, coordinateCount=30){
 const dir=await mkdtemp(join(tmpdir(),'arch-plt-metadata-'));
 const path=join(dir,'fixture.h5');
 try {
  const f=new h5.File(path,'w');
  try {
   f.create_attribute('time',0);f.create_attribute('dim',2);f.create_attribute('geometry','cartesian');
   const grid=f.create_group('Grid');
   for(const name of ['x','y','z'])grid.create_dataset({name,data:new Float64Array(coordinateCount)});
   for(const name of ['level','morton'])grid.create_dataset({name,data:new Int32Array([0,1])});
   f.create_group('Data').create_dataset({name:'DENS',data:new Float64Array(30),shape:[2,3,5]});
   change?.(f);
  } finally {f.close();}
  await run(path);
 } finally {await rm(dir,{recursive:true,force:true});}
}
test('non-square writer structure is metadata only, completion/units/identity remain unknown',async()=>{
 await fixture(async path=>{
  const original=Object.getOwnPropertyDescriptor(h5.Dataset.prototype,'value')!;
  Object.defineProperty(h5.Dataset.prototype,'value',{get(){throw new Error('Field array read forbidden in metadata audit');},configurable:true});
  try {
   const result=await inspectPlotfileMetadata(path);
   assert.deepEqual(result.cellShape,[3,5]);assert.equal(result.cells,30);
   assert.equal(result.completion.state,'unknown');assert.equal(result.renderEligible,false);
   assert.equal(result.fields[0].unit,null);assert.equal(result.scientificIdentity.case,null);
   assert.equal(result.file.sha256.length,64);
   assert.deepEqual(result.nativeCellGeometry,{bounds:'unavailable',volume:'unavailable'});
  } finally {Object.defineProperty(h5.Dataset.prototype,'value',original);}
 });
});
test('actual maintained Sod file headers read without changing bytes',async()=>{
 const path=new URL('./fixtures/sod-1d.h5',import.meta.url).pathname;
 const before=await readFile(path);
 const result=await inspectPlotfileMetadata(path);
 assert.equal(result.dimension,1);assert.equal(result.time,0.15);
 assert.deepEqual(result.fields.map(f=>f.name),['DENS','ENER','PRES','VELX']);
 assert.equal(result.renderEligible,false);assert.deepEqual(await readFile(path),before);
});
test('mismatched field shape and external data links reject',async()=>{
 await fixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/Field shapes/);},f=>{
  (f.get('Data') as InstanceType<typeof h5.Group>).create_dataset({name:'PRES',data:new Float64Array(2)});
 });
 await fixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/Missing local dataset/);},f=>{
  const data=f.get('Data') as InstanceType<typeof h5.Group>;
  data.create_external_link('/missing/never-open.h5','/payload','foreign');
 });

});
test('symlink, invalid HDF5 and empty file are rejected, handles recover',async()=>{
 await fixture(async path=>{
  const link=path+'.link';await symlink(path,link);
  await assert.rejects(inspectPlotfileMetadata(link));
  const invalid=path+'.invalid';await writeFile(invalid,'not hdf5');
  await assert.rejects(inspectPlotfileMetadata(invalid));
  await writeFile(invalid,'');await assert.rejects(inspectPlotfileMetadata(invalid),/non-empty/);
  assert.equal((await inspectPlotfileMetadata(path)).cells,30);
 });
});

test('coordinate lengths, field count and input byte budgets are enforced',async()=>{
 await fixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/Coordinate shape/);},undefined,29);
 await fixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/field audit budget/);},f=>{
  const data=f.get('Data') as InstanceType<typeof h5.Group>;
  for(let i=0;i<128;i++)data.create_dataset({name:'extra'+i,data:new Float64Array(30),shape:[2,3,5]});
 });
 await fixture(async path=>{
  const oversized=path+'.oversized';
  const fd=await open(oversized,'w');
  try{await fd.truncate(64*1024*1024+1);}finally{await fd.close();}
  await assert.rejects(inspectPlotfileMetadata(oversized),/at most 64 MiB/);
 });
});


test('writer partial naming is rejected before HDF5 access, even with readable legacy contents',async()=>{
 await fixture(async path=>{
  const temporary=path+'.partial-ABC123';
  await writeFile(temporary,await readFile(path));
  await assert.rejects(inspectPlotfileMetadata(temporary),/not a published Plotfile/);
 });
});
