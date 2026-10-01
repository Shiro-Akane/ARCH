import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createLatestRequest, selectedFile } from '../src/data/latestRequest.ts';

test('older success and failure cannot replace newer file results', async () => {
  for (const fail of [false,true]) {
    const run = createLatestRequest(); let shown = '';
    let resolve!: (value: string) => void; let reject!: (error: Error) => void;
    const slow = new Promise<string>((yes,no) => { resolve=yes; reject=no; });
    const first = run(() => slow, x => { shown=x; }, () => { shown='old error'; });
    await run(async () => 'new file', x => { shown=x; }, () => { shown='new error'; });
    if (fail) reject(new Error('old')); else resolve('old file');
    await first; assert.equal(shown,'new file');
  }
});
test('current errors are reported and next valid request recovers', async () => {
  const run=createLatestRequest(); let shown='';
  await run(async () => { throw new Error('read failed'); }, () => { shown='unexpected'; }, e => { shown=(e as Error).message; });
  assert.equal(shown,'read failed');
  await run(async () => 'valid', x => { shown=x; }, () => {}); assert.equal(shown,'valid');
});
test('cancelled chooser has no selected file and does not start a request', () => {
  assert.equal(selectedFile(null),null);
  assert.equal(selectedFile({length:0} as FileList),null);
});
