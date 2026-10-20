import { test } from 'node:test';
import assert from 'node:assert/strict';
import { loadPar, editPar, revertPar, parStatus } from '../src/state/parState.ts';
import { studioReducer, initialState } from '../src/state/studioState.ts';
test('loaded, dirty, invalid and revert states preserve loaded document',()=>{
 const s=loadPar('one.par','x=0.5'); assert.equal(parStatus(s),'saved');
 const edited=editPar(s,'x','0.7');assert.equal(parStatus(edited),'dirty');assert.equal(s.document.raw,'x=0.5');
 assert.equal(parStatus(editPar(s,'x','bad#comment')),'invalid');assert.equal(parStatus(revertPar(edited)),'saved');
 assert.equal(loadPar('two.par','y=1').filename,'two.par');
});
test('real config changes mark Mock stale and invalidate pending requests without changing Mock parameters',()=>{
 const s=initialState();const next=studioReducer({...s,preview:'current'},{type:'config/external-edit'});
 assert.equal(next.preview,'stale');assert.equal(next.working,s.working);assert.equal(next.request,null);
});
