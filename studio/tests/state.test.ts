import { test } from 'node:test';
import assert from 'node:assert/strict';
import { initialState, studioReducer, validate } from '../src/state/studioState.ts';
test('valid edits dirty the working copy and stale preview without mutating saved', () => {
 const s = initialState(); const edited = studioReducer(s, {type:'edit',key:'hotspot_x',value:'0.7'});
 assert.equal(edited.config,'dirty'); assert.equal(edited.preview,'stale'); assert.equal(edited.saved.hotspot_x,'0.5');
});
test('invalid config cannot generate, and correcting it restores dirty', () => {
 let s = studioReducer(initialState(),{type:'edit',key:'resolution_x',value:'1000000'});
 assert.equal(s.config,'invalid'); assert.equal(studioReducer(s,{type:'preview/start',revision:s.revision}).preview,'stale');
 s=studioReducer(s,{type:'edit',key:'resolution_x',value:'512'}); assert.equal(s.config,'dirty');
 assert.ok(validate({...s.working,xmax:'0'}).xmax); assert.ok(validate({...s.working,hotspot_x:''}).hotspot_x);
});
test('generation success/failure and obsolete completion are handled centrally', () => {
 let s=studioReducer(initialState(),{type:'preview/start',revision:0}); assert.equal(s.preview,'generating');
 assert.equal(studioReducer(s,{type:'preview/success',revision:0}).preview,'current');
 assert.equal(studioReducer(s,{type:'preview/failure',revision:0,message:'Unavailable'}).preview,'failed');
 s=studioReducer(s,{type:'edit',key:'hotspot_y',value:'0.8'});
 assert.equal(studioReducer(s,{type:'preview/success',revision:0}).preview,'stale');
 assert.equal(s.run,'idle');
});

test('field switching is instant and does not dirty configuration or restart generation', () => {
 const s=studioReducer(initialState(),{type:'field/select',field:'pressure'});
 assert.equal(s.field,'pressure'); assert.equal(s.config,'saved'); assert.equal(s.revision,0); assert.equal(s.request,null);
});

test('Save snapshots the working copy; Revert restores it and rejects stale requests', () => {
 let s=studioReducer(initialState(),{type:'edit',key:'hotspot_x',value:'0.7'});
 s=studioReducer(s,{type:'config/save'}); assert.equal(s.config,'saved');assert.equal(s.saved.hotspot_x,'0.7');assert.equal(s.preview,'stale');
 s=studioReducer(s,{type:'edit',key:'hotspot_x',value:'0.9'}); assert.equal(s.saved.hotspot_x,'0.7');
 s=studioReducer(s,{type:'preview/start',revision:s.revision}); const request=s.revision;
 s=studioReducer(s,{type:'config/revert'}); assert.equal(s.working.hotspot_x,'0.7');assert.equal(s.config,'saved');assert.equal(s.request,null);
 assert.equal(studioReducer(s,{type:'preview/success',revision:request}).preview,'stale');
});
test('invalid input cannot overwrite saved snapshot and Revert recovers it',()=>{
 let s=studioReducer(initialState(),{type:'edit',key:'hotspot_radius',value:''});
 const invalid=s;s=studioReducer(s,{type:'config/save'});assert.equal(s,invalid);
 s=studioReducer(s,{type:'config/revert'});assert.equal(s.working.hotspot_radius,'0.12');assert.equal(s.config,'saved');
});
