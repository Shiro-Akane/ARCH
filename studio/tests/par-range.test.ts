import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parSchema } from '../src/data/parSchema.ts';
import { loadPar,editPar,parStatus } from '../src/state/parState.ts';
test('slider metadata comes from explicit contract, not current numeric value',()=>{
 assert.deepEqual(parSchema.refine_threshold.range,[0,1]);assert.ok(parSchema.refine_threshold.evidence.includes('RuntimeParams'));
 assert.equal(parSchema.cfl.range,undefined);assert.equal(parSchema.tmax.range,undefined);assert.equal(parSchema.temperature,undefined);
});
test('precise values preserved and illegal range input invalid without clamping',()=>{
 const s=loadPar('range.par','refine_threshold=0.8');
 const precise=editPar(s,'refine_threshold','0.723456789123');assert.equal(precise.changes.refine_threshold,'0.723456789123');assert.equal(parStatus(precise),'dirty');
 const invalid=editPar(s,'refine_threshold','1.2');assert.equal(parStatus(invalid),'invalid');assert.equal(invalid.changes.refine_threshold,'1.2');
});
