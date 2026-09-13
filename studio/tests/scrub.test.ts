import test from 'node:test';import assert from 'node:assert/strict';import {scrubValue} from '../src/components/ParameterPanel/scrub.ts';
test('scrub threshold, fine, integer, bounded, raw and unbounded semantics',()=>{
 assert.equal(scrubValue('1e-8',3,false,false),'1e-8');assert.equal(scrubValue('10',10,false,false),'11');assert.equal(scrubValue('10',10,true,false),'10.1');
 assert.equal(scrubValue('0.9',100,false,false,[0,1]),'1');assert.equal(scrubValue('1',100,false,false),'2');assert.equal(scrubValue('-1',0,false,true),'-1');assert.equal(scrubValue('bad',50,false,false),'bad');assert.equal(scrubValue('',50,false,false),'');assert.ok(Number.isInteger(Number(scrubValue('10',17,false,true))));
});
