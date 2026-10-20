import test from 'node:test';
import assert from 'node:assert/strict';
import {previewInputIssue} from '../src/data/previewScheduling.ts';
import type {CoreParameter} from '../src/host/previewContracts.ts';
const parameters=[{key:'x_pos',type:'float'}] as CoreParameter[];
test('automatic Preview holds incomplete known numeric input without changing raw text',()=>{
 for(const token of ['-','+','.','1e','1e-','NaN','Infinity','1x','1e999','1e-999']){
  const text='x_pos = '+token+' # editing\nunknown = keep\n';
  assert.match(previewInputIssue(text,parameters)??'',/^x_pos:/);
  assert.equal(text,'x_pos = '+token+' # editing\nunknown = keep\n');
 }
 for(const token of ['.3','0.32','3e-1','-0.3'])assert.equal(previewInputIssue('x_pos='+token,parameters),undefined);
});
test('unknown model types and omitted keys never inherit invented numeric constraints',()=>{
 assert.equal(previewInputIssue('custom=-',parameters),undefined);
 assert.equal(previewInputIssue('x_pos=-',undefined),undefined);
 assert.equal(previewInputIssue('# missing x_pos',parameters),undefined);
});
