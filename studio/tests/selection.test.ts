import {test} from 'node:test';
import assert from 'node:assert/strict';
import {inspectPoint} from '../src/data/selection.ts';
import {buildMockPreview} from '../src/data/MockPreviewProvider.ts';
import {defaults, initialState, studioReducer} from '../src/state/studioState.ts';
test('point maps to containing cell center, including offset domains and upper edge',()=>{
 const data=buildMockPreview({...defaults,resolution_x:'4',resolution_y:'2',xmin:'-2',xmax:'2',ymin:'10',ymax:'12'});
 const p=inspectPoint(data,-0.2,10.8)!; assert.equal(p.index,1); assert.equal(p.x,-0.5); assert.equal(p.y,10.5); assert.equal(p.temperature,data.temperature.values[1]);
 assert.equal(inspectPoint(data,2,12)?.index,7); assert.equal(inspectPoint(data,2.1,12),null); assert.equal(inspectPoint(data,NaN,11),null);
});
test('selection survives field switching but edits clear it and stale data cannot be selected',()=>{
 let s=studioReducer(initialState(),{type:'preview/start',revision:0});
 s=studioReducer(s,{type:'preview/success',revision:0,data:buildMockPreview(defaults)});
 s=studioReducer(s,{type:'point/select',x:0.5,y:0.5});assert.ok(s.selected);
 s=studioReducer(s,{type:'field/select',field:'temperature'});assert.ok(s.selected);
 s=studioReducer(s,{type:'edit',key:'hotspot_x',value:'0.7'});assert.equal(s.selected,null);
 assert.equal(studioReducer(s,{type:'point/select',x:0.5,y:0.5}).selected,null);
});

test('Revert recognizes an existing matching preview and Save leaves current data untouched',()=>{
 let s=studioReducer(initialState(),{type:'preview/start',revision:0});
 const data=buildMockPreview(defaults);s=studioReducer(s,{type:'preview/success',revision:0,data});
 s=studioReducer(s,{type:'edit',key:'hotspot_x',value:'0.8'});s=studioReducer(s,{type:'config/revert'});
 assert.equal(s.preview,'current');assert.equal(s.data,data);
 s=studioReducer(s,{type:'config/save'});assert.equal(s.preview,'current');assert.equal(s.data,data);
});
