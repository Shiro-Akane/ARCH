import test from 'node:test';import assert from 'node:assert/strict';
import {EditHistory,historyShortcut} from '../src/state/editHistory.ts';
test('numeric enum bool edits undo/redo and a full drag is one step',()=>{
 const h=new EditHistory<{value:string}>();let v={value:'1'};
 for(const value of ['2','hllc','true']){const n={value};h.record(v,n);v=n;}
 assert.deepEqual(h.undo(v),{value:'hllc'});v=h.redo({value:'hllc'});assert.equal(v.value,'true');
 h.begin(v);for(let i=0;i<100;i++){const n={value:String(i)};h.record(v,n);v=n;}h.end();
 assert.equal(h.past.length,4);assert.equal(h.undo(v).value,'true');h.reset();assert.equal(h.past.length,0);
});
test('navigation has no edit history action; supported shortcuts are explicit',()=>{
 assert.equal(historyShortcut('Tab',false,false,false),null);assert.equal(historyShortcut('z',true,false,false),'undo');assert.equal(historyShortcut('z',true,false,true),'redo');assert.equal(historyShortcut('y',true,false,false),'redo');
});

test('cancelled drag restores start without an undo entry and edit after undo discards redo',()=>{
 const h=new EditHistory<string>();h.begin('start');h.record('start','middle');h.record('middle','start');h.end();assert.equal(h.past.length,0);
 h.record('start','a');h.undo('a');h.record('start','b');assert.equal(h.future.length,0);
});
