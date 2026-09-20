import test from 'node:test';import assert from 'node:assert/strict';import {readFileSync} from 'node:fs';
import {validateCorePreview,validateModelCapabilities} from '../src/host/previewValidation.ts';
import {realInitGrid,gridPoint,sampleEdges} from '../src/data/RealInitPreviewProvider.ts';
const fixture=(name:string)=>JSON.parse(readFileSync(new URL('../../src/api/examples/core-b/'+name+'.json',import.meta.url),'utf8'));
test('actual Core B non-square responses preserve axes and j*Nx+i in both directions',()=>{
 for(const name of ['cellular-x1','cellular-x2']){const core=fixture(name);validateCorePreview(core,core.identity,core.data.sampling.shape);const result={core} as never;
 for(const field of core.data.fields){const g=realInitGrid(result,field.key);assert.notEqual(g.width,g.height);for(let j=0;j<g.height;j++)for(let i=0;i<g.width;i++){const point=gridPoint(g,g.x[i],g.y[j]);assert.equal(point.index,j*g.width+i);assert.equal(g.values[point.index],field.values[j*g.width+i]);}}
 }
});
test('2D schema rejects transpose, axes/order/fixed coordinate/count corruption',()=>{
 for(const change of [(c:ReturnType<typeof fixture>)=>c.data.sampling.shape.reverse(),(c:ReturnType<typeof fixture>)=>c.data.axes.reverse(),(c:ReturnType<typeof fixture>)=>c.data.sampling.order='x2-fastest',(c:ReturnType<typeof fixture>)=>c.data.sampling.fixedCoordinates[0].value=1,(c:ReturnType<typeof fixture>)=>c.data.fields[0].values.pop(),(c:ReturnType<typeof fixture>)=>c.data.axes[1].values.reverse()]){const c=fixture('cellular-x1');change(c);assert.throws(()=>validateCorePreview(c,c.identity));}
});
test('model capabilities expose independent limits and reject unsafe budgets',()=>{const caps=fixture('capabilities');const models=validateModelCapabilities(caps.modelCapabilities);assert.deepEqual(models[1].sampling.defaultShape,[128,128]);assert.equal(models[1].maxResponseBytes,8388608);caps.modelCapabilities[1].sampling.maxTotalSamples=1e9;assert.throws(()=>validateModelCapabilities(caps.modelCapabilities));});
test('real structured errors retain identity and state without data',()=>{for(const name of ['missing-eos','response-limit','unsupported-direction','sampling-limit']){const c=fixture(name);assert.equal(validateCorePreview(c,c.identity).status,'error');assert.equal(c.data,null);}});

test('bin centers map to heatmap edges without half-sample shift',()=>{const x=Float64Array.of(2.56,7.68,12.8,17.92,23.04);const edges=sampleEdges(x);assert.equal(edges.length,6);assert.ok(Math.abs(edges[0])<1e-12);assert.ok(Math.abs(edges[5]-25.6)<1e-12);for(let i=0;i<x.length;i++)assert.ok(Math.abs((edges[i]+edges[i+1])/2-x[i])<1e-12);});
