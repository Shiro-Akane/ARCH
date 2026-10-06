import {test} from 'node:test';
import assert from 'node:assert/strict';
import {copyFile,mkdtemp,rm} from 'node:fs/promises';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import h5 from 'h5wasm/node';
import {inspectPlotfileMetadata,readPlotfileFieldSlice} from '../host/plotfileMetadata.ts';
import {validatePlotfileAudit} from '../src/host/plotfileAudit.ts';
import {validFieldDeclaration,validMeasureLabels} from '../src/host/plotfileDeclarations.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
await h5.ready;
const declaration={version:'candidate-field-1',unit:'g/cm^3',centering:'cell',basis:'scalar',meaning:'mass_density',unitReason:null};
const identity={protocolVersion:PROTOCOL_VERSION,projectId:'p',relativePath:'f.h5'};
async function fixture(run:(p:string)=>Promise<void>,change:Record<string,string>={}) {
 const dir=await mkdtemp(join(tmpdir(),'arch-field-declarations-')),path=join(dir,'f.h5');
 try {
  await copyFile(new URL('./fixtures/sod-1d.h5',import.meta.url),path);
  const f=new h5.File(path,'a');
  try {
   const d=f.get('Data/DENS');
   assert.ok(d instanceof h5.Dataset);
   for(const [k,v] of Object.entries({metadata_version:'candidate-field-1',unit:'g/cm^3',centering:'cell',basis:'scalar',meaning:'mass_density',...change}))d.create_attribute(k,v);
  }finally{f.close();}
  await run(path);
 }finally{await rm(dir,{recursive:true,force:true});}
}
test('recorded producer field declarations reach metadata and raw slice, without fabricating legacy units',async()=>{
 await fixture(async path=>{
  const metadata=await inspectPlotfileMetadata(path);
  assert.deepEqual(metadata.fields.find(f=>f.name==='DENS')?.declaration,declaration);
  assert.equal(metadata.fields.find(f=>f.name==='PRES')?.unit,null);
  const result=await readPlotfileFieldSlice(path,{field:'DENS',block:0,start:[0],count:[1]});
  assert.equal(result.payload?.unit,'g/cm^3');
  const selection={field:'DENS',block:0,start:[0],count:[1]};
  assert.equal(validatePlotfileAudit({...identity,result},'p','f.h5',selection,result.file.sha256).audit.payload?.unit,'g/cm^3');
  assert.equal(result.renderEligible,false);assert.equal(result.completion.state,'unknown');
  assert.throws(()=>validatePlotfileAudit({...identity,result:{...result,payload:{...result.payload,unit:'kg/m^3'}}},'p','f.h5',selection,result.file.sha256),/payload/);
 });
});
test('unknown units retain an explicit reason; arbitrary unit assignment needs the recorded declaration',async()=>{
 await fixture(async path=>{
  const metadata=await inspectPlotfileMetadata(path),f=metadata.fields.find(f=>f.name==='DENS')!;
  assert.equal(f.unit,null);assert.equal(f.declaration?.unitReason,'Local EOS exponent');
  const fabricated={...metadata,fields:metadata.fields.map(f=>f.name==='PRES'?{...f,unit:'guessed'}:f)};
  assert.throws(()=>validatePlotfileAudit({...identity,metadata:fabricated},'p','f.h5'));
 },{unit:'unknown',unit_reason:'Local EOS exponent'});
});
test('unknown version, missing unknown reason, oversized strings and face centering fail closed',async()=>{
 for(const change of [{metadata_version:'future'},{unit:'unknown'},{unit_reason:'x'.repeat(300)},{centering:'face'}])
  await fixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/declaration|metadata/i);},change);
});
test('field meaning/basis/unknown reasons and low-dimensional measures remain distinct',()=>{
 assert.ok(validFieldDeclaration(declaration));
 assert.ok(validFieldDeclaration({...declaration,unit:null,unitReason:'Gamma1 varies'}));
 assert.equal(validFieldDeclaration({...declaration,unit:null,unitReason:null}),false);
 assert.ok(validMeasureLabels('cm','per_unit_transverse_area',1));
 assert.ok(validMeasureLabels('cm^2','per_unit_transverse_length',2));
 assert.equal(validMeasureLabels('cm^2','per_unit_transverse_length',1),false);
 assert.equal(validMeasureLabels('cm','total_3d_volume',1),false);
 assert.equal(validMeasureLabels(null,'per_unit_transverse_area'),false);
 assert.ok(validMeasureLabels(null,undefined));
});
