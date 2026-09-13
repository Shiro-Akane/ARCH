import { test } from 'node:test';
import assert from 'node:assert/strict';
import h5 from 'h5wasm';
import { inspectPlotfile, readPlotfileField } from '../src/data/PlotfilePreviewProvider.ts';
import { readFile } from 'node:fs/promises';

const { FS } = await h5.ready;
let serial = 0;
function fixture(change?: (f: InstanceType<typeof h5.File>) => void): File {
  const path = `/fixture-${++serial}.h5`;
  const f = new h5.File(path, 'w');
  f.create_attribute('time', 0.15); f.create_attribute('dim', 1); f.create_attribute('geometry', 'cartesian');
  f.create_group('Grid').create_dataset({ name: 'x', data: new Float64Array([0.25, 0.75]) });
  f.create_group('Data').create_dataset({ name: 'unknown_species', data: new Float64Array([2, 4]) });
  change?.(f); f.close();
  const bytes = FS.readFile(path); FS.unlink(path);
  return new File([bytes], 'fixture.h5');
}
test('real existing Sod metadata, fields and sample values', async () => {
  const bytes = await readFile(new URL('./fixtures/sod-1d.h5', import.meta.url));
  const file = new File([bytes], 'Sod.h5');
  const info = await inspectPlotfile(file);
  assert.deepEqual(info, { file: 'Sod.h5', time: 0.15, dimension: 1, geometry: 'cartesian', fields: ['DENS','ENER','PRES','VELX'] });
  const line = await readPlotfileField(file, 'PRES');
  assert.equal(line.x.length, 64); assert.equal(line.min, 0.1); assert.equal(line.max, 1);
  assert.equal(line.x[32], 0.5078125); assert.equal(line.values[32], 0.3054751636143017);
});
test('unknown field discovery and actual values', async () => {
  const file = fixture(); assert.deepEqual((await inspectPlotfile(file)).fields, ['unknown_species']);
  assert.deepEqual([...(await readPlotfileField(file, 'unknown_species')).values], [2,4]);
});
test('non HDF5, non ARCH HDF5, missing metadata and Data fail safely', async () => {
  await assert.rejects(inspectPlotfile(new File(['not hdf5'], 'invalid.h5')), /Could not open/);
  for (const key of ['time','dim','geometry']) await assert.rejects(inspectPlotfile(fixture(f => f.delete_attribute(key))), /metadata|missing|invalid/);
  const path = '/not-arch.h5'; const f = new h5.File(path,'w'); f.close();
  const bytes = FS.readFile(path); FS.unlink(path);
  await assert.rejects(inspectPlotfile(new File([bytes], 'other.h5')), /missing|invalid/);
  const p = '/no-data.h5'; const g = new h5.File(p,'w');
  g.create_attribute('time',0); g.create_attribute('dim',1); g.create_attribute('geometry','cartesian'); g.close();
  const b = FS.readFile(p); FS.unlink(p); await assert.rejects(inspectPlotfile(new File([b],'nodata.h5')), /Data/);
});
test('unsupported field types, empty and nonfinite fields reject; valid field recovers', async () => {
  const file = fixture(f => {
    const data = f.get('Data') as InstanceType<typeof h5.Group>;
    data.create_dataset({name:'text',data:['a','b']});
    data.create_dataset({name:'empty',data:new Float64Array([])});
    data.create_dataset({name:'nan',data:new Float64Array([NaN,1])});
    data.create_dataset({name:'inf',data:new Float64Array([Infinity,1])});
  });
  for (const field of ['text','empty','nan','inf']) await assert.rejects(readPlotfileField(file,field), /Unsupported/);
  assert.equal((await readPlotfileField(file,'unknown_species')).max,4);
});
test('read failure and size limits reject without leaking virtual files', async () => {
  const before = FS.readdir('/').filter((x: string) => x.startsWith('plotfile-'));
  const file = new File(['bytes'],'failure.h5'); file.arrayBuffer = async () => { throw new Error('Read failed'); };
  await assert.rejects(inspectPlotfile(file), /Read failed/);
  await assert.rejects(inspectPlotfile(new File([], 'empty.h5')), /non-empty/);
  assert.deepEqual(FS.readdir('/').filter((x: string) => x.startsWith('plotfile-')), before);
});
