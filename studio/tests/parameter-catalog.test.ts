import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {catalog,catalogCoordinates,parameterUnit} from '../src/data/parameterCatalog.ts';
import {validateConfigurationSchema} from '../src/host/configurationValidation.ts';
import {loadPar,editPar,exportPar} from '../src/state/parState.ts';
const schema=validateConfigurationSchema(JSON.parse(await readFile(new URL('../../src/api/examples/configuration/schema.json',import.meta.url),'utf8')));
const requirelessFixture=await readFile(new URL('../../src/api/examples/configuration/inspect-sod.json',import.meta.url),'utf8');
test('all 90 schema keys are represented, aliases share one edit destination and defaults do not serialize',()=>{
 const rows=catalog(schema.parameters,{timeintegrator:'RK1'});
 assert.equal(rows.flatMap(r=>[r.parameter.key,...r.aliases]).length,90);
 assert.equal(rows.find(r=>r.parameter.key==='time_integrator')?.sourceKey,'timeintegrator');
 assert.equal(catalog(schema.parameters,{time_integrator:'RK2',timeintegrator:'RK1'}).find(r=>r.parameter.key==='time_integrator')?.sourceKey,'time_integrator');
 const raw='# retained\r\ntimeintegrator = RK1 # alias\r\nunknown = untouched\r\n';const state=loadPar('a.par',raw);
 assert.equal(exportPar(state,schema.parameters).text,raw);
 const row=rows.find(r=>r.parameter.key==='time_integrator')!;
 const output=exportPar(editPar(state,row.sourceKey,'RK3'),schema.parameters).text;
 assert.match(output,/timeintegrator = RK3 # alias/);assert.doesNotMatch(output,/time_integrator =/);assert.match(output,/unknown = untouched\r\n/);
 const inserted=exportPar(editPar(state,'ode_max_substeps','44'),schema.parameters).text;
 assert.match(inserted,/ode_max_substeps = 44/);assert.doesNotMatch(inserted,/ode_rtol/);
});
test('coordinate layout follows all nine Core conventions and never consumes transient invalid block tokens',()=>{
 for(const geometry of ['cartesian','cylindrical','spherical'])for(const dimension of [1,2,3]){
  const c=catalogCoordinates(schema,{geometry,nblockx1:'2',nblockx2:dimension>=2?'1':'0',nblockx3:dimension===3?'2':'0'});
  assert.equal(c?.dimension,dimension);assert.equal(c?.axes.filter(a=>a.active).length,dimension);
  if(geometry==='cylindrical'&&dimension===2)assert.deepEqual(c?.axes.filter(a=>a.active).map(a=>a.displayName),['r','phi']);
 }
 for(const value of ['','-','1.','1.5','-1','2147483648'])assert.equal(catalogCoordinates(schema,{nblockx1:'1',nblockx2:value,nblockx3:'0'}),undefined);
 assert.equal(catalogCoordinates(schema,{nblockx1:'1',nblockx2:'0',nblockx3:'1'}),undefined);
});
test('units retain Core status; coordinate values are inherited only from provided metadata',()=>{
 const p=schema.parameters.find(p=>p.key==='x1_min')!;
 assert.match(parameterUnit(p),/Unit not provided.*coordinate-dependent/);
 const inspected=JSON.parse(requirelessFixture);
 assert.match(parameterUnit(p,undefined,inspected.coordinates),/code_length/);
});
