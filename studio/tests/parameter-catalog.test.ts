import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {catalog,catalogCoordinates,parameterUnit} from '../src/data/parameterCatalog.ts';
import {validateConfigurationSchema} from '../src/host/configurationValidation.ts';
import {loadPar,editPar,exportPar} from '../src/state/parState.ts';
const schema=validateConfigurationSchema(JSON.parse(await readFile(new URL('../../src/api/examples/configuration-v3/schema.json',import.meta.url),'utf8')));

test('current schema is dynamic, retired aliases are not destinations, missing values stay blank',()=>{
 const rows=catalog(schema.parameters,{timeintegrator:'RK1'});
 assert.equal(rows.length,94);
 assert.equal(rows.find(r=>r.parameter.key==='time_integrator')?.sourceKey,'time_integrator');
 assert.ok(!rows.some(r=>r.aliases.includes('timeintegrator')));
 const raw='# retained\r\nnblockx2 = 0\r\nnblockx3 = 0\r\nunknown = untouched\r\n';
 const state=loadPar('a.par',raw);assert.equal(exportPar(state,schema.parameters).text,raw);
 for(const key of ['gravity_boundary','gravity_rtol','gravity_atol','gravity_max_cycles','eos_coulomb_mult','hll_wave_speed','dt_max','dt_init','dt_min','tstep_change_factor']){
  const row=rows.find(r=>r.parameter.key===key)!;
  assert.ok(row);assert.equal(row.value,'');assert.ok(row.parameter.presentation?.description);
  const output=exportPar(editPar(state,key,String(({gravity_boundary:'periodic',gravity_rtol:1e-8,gravity_atol:0,gravity_max_cycles:50,eos_coulomb_mult:1,hll_wave_speed:'roe',dt_max:-1,dt_init:0.001,dt_min:1e-20,tstep_change_factor:1.1} as Record<string,string|number>)[key])),schema.parameters).text;
  assert.ok(output.startsWith(raw));assert.ok(output.includes(key+' = '));
  for(const other of rows.filter(r=>r.parameter.key!==key&&!raw.includes(r.parameter.key)))assert.ok(!output.includes(other.parameter.key+' = '));
 }
 const smaller=structuredClone(schema);smaller.parameters=smaller.parameters.slice(0,12);
 assert.equal(validateConfigurationSchema(smaller).parameters.length,12);
 const larger=structuredClone(schema);larger.parameters.push({...larger.parameters[0],key:'future_parameter'});
 assert.equal(validateConfigurationSchema(larger).parameters.length,95);
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
 const inspected={coordinates:catalogCoordinates(schema,{geometry:'cartesian',nblockx1:'8',nblockx2:'0',nblockx3:'0'})!};
 // Consume the Core coordinate catalog only after explicit valid topology.
 assert.equal(inspected.coordinates.axes[0].unit,'cm');
 assert.equal(parameterUnit(p,undefined,inspected.coordinates),'cm · coordinate-dependent');
 const unavailable=structuredClone(inspected.coordinates);unavailable.axes[0].unit=null;
 assert.match(parameterUnit(p,undefined,unavailable),/Unit not provided/);
});
