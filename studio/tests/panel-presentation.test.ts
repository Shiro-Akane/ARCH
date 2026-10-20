import {test} from 'node:test';
import assert from 'node:assert/strict';
import {observedDimension,inactiveAxisKey,blockSummary,advancedKey,matchesParameter} from '../src/components/ParameterPanel/panelPresentation.ts';
test('dimension-aware presentation uses explicit valid topology and never discards data',()=>{
 for(const [a,b,dim] of [['0','0',1],['4','0',2],['4','2',3]] as const){
  const values={nblockx2:a,nblockx3:b};assert.equal(observedDimension(values),dim);
  assert.equal(inactiveAxisKey('x3_min',dim),dim<3);assert.deepEqual(values,{nblockx2:a,nblockx3:b});
 }
 assert.equal(observedDimension({}),null);assert.equal(observedDimension({nblockx2:'0',nblockx3:'1'}),null);
 assert.equal(inactiveAxisKey('x2_min',null),false);
});
test('summaries do not invent defaults or evaluate scientific quality',()=>{
 assert.equal(blockSummary('Runtime',{}),'—');assert.equal(blockSummary('Runtime',{tmax:'1e-8'}),'tmax 1e-8');
 assert.equal(blockSummary('Grid',{geometry:'cartesian',nblockx1:'4',nblockx2:'0',nblockx3:'0'}),'cartesian · 1D · 4 blocks X');
});

test('search discovers advanced and custom keys without inventing categories',()=>{
 assert.equal(advancedKey('x2_min','Grid',1),true);assert.equal(matchesParameter('x2_min','0','x2'),true);
 assert.equal(matchesParameter('geometry','cartesian','Geometry'),true);assert.equal(matchesParameter('custom_untyped','0.5','untyped'),true);
 assert.equal(matchesParameter('custom_untyped','0.5','not found'),false);
});
