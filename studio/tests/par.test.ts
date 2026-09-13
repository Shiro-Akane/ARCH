import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { parsePar, effectiveEntries, serializePar } from '../src/data/ParDocument.ts';
test('representative real files round trip without byte changes', () => {
  for (const name of ['sod','cellular','gaussian']) {
    const raw = readFileSync(new URL(`./fixtures/${name}.par`,import.meta.url),'utf8');
    assert.equal(serializePar(parsePar(raw)),raw);
  }
});
test('comments, blank lines, unknown lines and literals preserved; last duplicate wins', () => {
  const raw = '# comment\r\n\r\na = 1e-5 # first\r\nunknown line\r\na = 2*pi  # effective\r\nflag=TRUE\r\npath=some=file';
  const doc=parsePar(raw);
  assert.equal(effectiveEntries(doc).find(e => e.key==='a')?.value,'2*pi');
  assert.deepEqual(doc.rawLines,[4]);
  assert.equal(serializePar(doc,{a:'0.125'}),raw.replace('2*pi','0.125'));
  assert.equal(serializePar(doc),raw);
});
test('BOM, empty values and no trailing newline survive; unsafe value injection rejected', () => {
  const raw='\uFEFFkey =   # note\nother=0.5'; const doc=parsePar(raw);
  assert.equal(doc.entries[0].key,'\uFEFFkey');
  assert.equal(serializePar(doc,{'\uFEFFkey':'hello'}),'\uFEFFkey =   hello# note\nother=0.5');
  assert.throws(() => serializePar(doc,{'\uFEFFkey':'x#bad'}));
  assert.throws(() => parsePar('x=\0'));
});
