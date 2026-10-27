import test from 'node:test';
import assert from 'node:assert/strict';
import {selectTestFiles,windowsOnlyFiles} from './run-platform.mjs';

const shared=['host-process.test.ts','host-windows-run.test.ts','future-shared.test.ts'];
const inventory=[...windowsOnlyFiles,...shared];

test('Linux selects every shared and unknown file while declaring only exact native-only files inapplicable',()=>{
 const result=selectTestFiles(inventory,'linux');
 assert.deepEqual(result.selected,[...shared].sort());
 assert.deepEqual(result.notApplicable,[...windowsOnlyFiles]);
 assert(result.selected.includes('host-windows-run.test.ts'),'mixed Windows/shared tests must stay selected');
});

test('Windows retains the entire file inventory without platform omissions',()=>{
 const result=selectTestFiles(inventory,'win32');
 assert.deepEqual(result.selected,[...inventory].sort());
 assert.deepEqual(result.notApplicable,[]);
});

test('empty applicable inventories and stale native file declarations fail closed',()=>{
 assert.throws(()=>selectTestFiles([],'linux'),/empty/);
 assert.throws(()=>selectTestFiles(shared,'linux'),/Missing declared/);
 assert.throws(()=>selectTestFiles([...windowsOnlyFiles],'linux'),/Applicable test inventory/);
});
