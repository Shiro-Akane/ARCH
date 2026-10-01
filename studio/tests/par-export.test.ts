import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { loadPar,editPar,revertPar,exportPar } from '../src/state/parState.ts';
test('Save As no-edit exact text and single-edit diff, original remains unchanged',()=>{
 const raw=readFileSync(new URL('./fixtures/sod.par',import.meta.url),'utf8');const s=loadPar('Sod.par',raw);
 assert.equal(exportPar(s).text,raw);assert.equal(exportPar(s).filename,'Sod_modified.par');
 const edited=editPar(s,'tmax','2.5e-1');assert.equal(exportPar(edited).text,raw.replace('tmax = 0.15','tmax = 2.5e-1'));
 assert.equal(exportPar(revertPar(edited)).text,raw);assert.equal(s.document.raw,raw);
 assert.throws(()=>exportPar(editPar(s,'tmax','NaN')),/invalid/);
});
