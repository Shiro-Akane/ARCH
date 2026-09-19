import test from 'node:test';import assert from 'node:assert/strict';
import {corePayload} from './preview-fixture.ts';
import {validateCorePreview,validateRealResult} from '../src/host/previewValidation.ts';
import {canAcceptRevision} from '../src/data/RealInitPreviewProvider.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
test('Core validator rejects wrong schema, identity, dimension, nonfinite arrays and duplicate fields',()=>{
 const base=corePayload();assert.equal(validateCorePreview(base,base.identity,2).data?.dimension,1);
 const edits:((v:ReturnType<typeof corePayload>)=>void)[]=[v=>{v.schemaVersion='2';},v=>{v.identity.requestId='wrong';},v=>{v.identity.configRevision='wrong';},v=>{v.identity.caseId='Other';},v=>{v.data.dimension=2;},v=>{v.data.axes[0].values=[.25];},v=>{v.data.axes[0].values=[.75,.25];},v=>{v.data.fields[0].values=[NaN,1];},v=>{v.data.fields[0].values=[Infinity,1];},v=>{v.data.fields[0].values=[1];},v=>{v.data.fields.push(v.data.fields[0]);},v=>{v.data.fields[0].min=2;},v=>{v.execution.timeStepping='executed';}];
 for(const edit of edits){const v=structuredClone(base);edit(v);assert.throws(()=>validateCorePreview(v,base.identity,2));}
 const identity={...base.identity,caseId:'Sod' as const,projectId:'p',buildId:'b',binarySha256:'s',profileId:'f'};const envelope={protocolVersion:PROTOCOL_VERSION,identity,generatedAt:'now',core:base};assert.ok(validateRealResult(envelope,identity));assert.throws(()=>validateRealResult({...envelope,identity:{...identity,buildId:'old'}},identity));
});
test('obsolete text, project or invalid Working Copy cannot accept a completed request',()=>{const start={text:'A',projectId:'p'};assert.equal(canAcceptRevision(start,{...start,valid:true}),true);for(const current of [{text:'B',projectId:'p',valid:true},{text:'A',projectId:'q',valid:true},{...start,valid:false}])assert.equal(canAcceptRevision(start,current),false);});

import {lineDomain} from '../src/data/LinePreviewData.ts';
test('real line domain follows large, negative and constant data rather than 0..1',()=>{assert.deepEqual(lineDomain({min:1e23,max:2.3e24}),[1e23,2.3e24]);assert.deepEqual(lineDomain({min:-9,max:-3}),[-9,-3]);for(const v of [0,1e24,-1e24,Number.MAX_VALUE,-Number.MAX_VALUE,Number.MIN_VALUE]){const [lo,hi]=lineDomain({min:v,max:v});assert.ok(Number.isFinite(lo)&&Number.isFinite(hi)&&lo<hi&&lo<=v&&hi>=v);}});
