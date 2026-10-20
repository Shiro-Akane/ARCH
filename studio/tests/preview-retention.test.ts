import test from 'node:test';import assert from 'node:assert/strict';import {readFile} from 'node:fs/promises';
import {initialState,studioReducer} from '../src/state/studioState.ts';
import {buildMockPreview} from '../src/data/MockPreviewProvider.ts';
import {loadPar,exportPar} from '../src/state/parState.ts';
test('never generated differs from successful preview after edit, real import and source changes',async()=>{
 let s=initialState();assert.equal(s.data,null);
 s=studioReducer(s,{type:'preview/start',revision:s.revision});
 const first=buildMockPreview(s.working);s=studioReducer(s,{type:'preview/success',revision:s.revision,data:first});
 assert.equal(s.preview,'current');
 s=studioReducer(s,{type:'edit',key:'hotspot_x',value:'0.7'});assert.equal(s.data,first);assert.equal(s.preview,'stale');assert.equal(s.request,null);
 const raw=await readFile(new URL('./fixtures/sod.par',import.meta.url),'utf8');assert.equal(exportPar(loadPar('sod.par',raw)).text,raw);
 for(let i=0;i<3;i++){s=studioReducer(s,{type:'config/external-edit'});assert.equal(s.data,first);assert.equal(s.preview,'stale');}
 s=studioReducer(s,{type:'preview/start',revision:s.revision});assert.equal(s.data,first);
 assert.equal(studioReducer(s,{type:'preview/start',revision:s.revision}),s);
 const next=buildMockPreview(s.working);s=studioReducer(s,{type:'preview/success',revision:s.revision,data:next});
 assert.equal(s.data,next);assert.notDeepEqual(next.density.values,first.density.values);assert.equal(s.preview,'current');
});
test('config switch cancels an in-flight request without accepting late data or losing last image',()=>{
 let s=initialState();s=studioReducer(s,{type:'preview/start',revision:0});const data=buildMockPreview(s.working);s=studioReducer(s,{type:'preview/success',revision:0,data});
 s=studioReducer(s,{type:'preview/start',revision:0});s=studioReducer(s,{type:'config/external-edit'});
 assert.equal(studioReducer(s,{type:'preview/success',revision:0,data:buildMockPreview({...s.working,hotspot_x:'0.1'})}),s);assert.equal(s.data,data);
});
