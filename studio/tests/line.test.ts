import { test } from 'node:test';
import assert from 'node:assert/strict';
import { adaptUniformLine, nearestSample } from '../src/data/LinePreviewData.ts';
test('1D adapter preserves unknown field name, pairs sorted coordinates and computes range', () => {
  const line = adaptUniformLine('species_abc', new Float64Array([2,0,1]), new Float64Array([30,10,20]));
  assert.equal(line.field, 'species_abc'); assert.deepEqual([...line.values], [10,20,30]);
  assert.equal(line.min,10); assert.equal(line.max,30); assert.equal(nearestSample(line,1.2),1); assert.equal(nearestSample(line,3),null);
});
test('reject empty, nonfinite, duplicate and nonuniform data', () => {
  for (const [x,y] of [[[],[]],[[0],[Infinity]],[[NaN],[1]],[[0,0],[1,2]],[[0,1,3],[1,2,3]],[[0,1],[1]]]) {
    assert.throws(() => adaptUniformLine('raw',new Float64Array(x),new Float64Array(y)));
  }
});
