import { test } from 'node:test';
import assert from 'node:assert/strict';
import { defaults } from '../src/state/studioState.ts';
import { buildMockPreview, generateMockPreview } from '../src/data/MockPreviewProvider.ts';
test('default contract has three distinct finite 512-square fields with exact bounds', () => {
 const fields=buildMockPreview(defaults);
 for (const field of Object.values(fields)) {
  assert.equal(field.width,512); assert.equal(field.height,512); assert.equal(field.values.length,512*512);
  assert.ok(field.values instanceof Float32Array); assert.deepEqual(field.xRange,[0,1]); assert.deepEqual(field.yRange,[0,1]);
  assert.equal(field.metadata?.scientific,false);
  let min=Infinity,max=-Infinity; for(const v of field.values) {assert.ok(Number.isFinite(v));min=Math.min(min,v);max=Math.max(max,v);}
  assert.equal(field.min,min); assert.equal(field.max,max);
 }
 assert.notDeepEqual(fields.density.values,fields.temperature.values); assert.notDeepEqual(fields.temperature.values,fields.pressure.values);
});
test('hotspot location, radius and amplitude affect density; preview snapshots are independent', () => {
 const p={...defaults,resolution_x:'64',resolution_y:'64'};
 const base=buildMockPreview(p).density;
 const moved=buildMockPreview({...p,hotspot_x:'0.75',hotspot_y:'0.25'}).density;
 const peak=moved.values.indexOf(moved.max); assert.ok(Math.abs(peak%64-47.5)<=1); assert.ok(Math.abs(Math.floor(peak/64)-15.5)<=1);
 const wider=buildMockPreview({...p,hotspot_radius:'0.25'}).density;
 assert.ok(wider.values.filter(v=>v>1.5).length > base.values.filter(v=>v>1.5).length);
 assert.ok(buildMockPreview({...p,hotspot_temperature:'2'}).density.max>base.max);
 assert.notEqual(base.values,moved.values);
});
test('invalid or overflowing parameters fail safely and asynchronous generation is cancellable', async () => {
 assert.throws(()=>buildMockPreview({...defaults,resolution_x:'999999'}));
 assert.throws(()=>buildMockPreview({...defaults,hotspot_temperature:'1e100'}));
 const c=new AbortController(); const pending=generateMockPreview(defaults,c.signal); c.abort(); await assert.rejects(pending,/cancelled/);
 const result=await generateMockPreview({...defaults,resolution_x:'4',resolution_y:'4'},new AbortController().signal); assert.equal(result.density.values.length,16);
});

test('Mock temperature is radially symmetric with no directional gradient',()=>{
 const field=buildMockPreview({...defaults,resolution_x:'32',resolution_y:'32'}).temperature;
 for(let y=0;y<32;y++) for(let x=0;x<32;x++) assert.equal(field.values[y*32+x],field.values[y*32+31-x]);
});
