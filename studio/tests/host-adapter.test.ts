import test from 'node:test';import assert from 'node:assert/strict';import {HttpLocalHostAdapter,validateSnapshot} from '../src/host/LocalHostAdapter.ts';
test('adapter restricts connection origin and rejects offline, malformed and incompatible hosts',async()=>{
 assert.throws(()=>new HttpLocalHostAdapter('http://192.168.0.1:4180'));assert.throws(()=>new HttpLocalHostAdapter('https://127.0.0.1:4180'));
 await assert.rejects(new HttpLocalHostAdapter(undefined,async()=>{throw new Error('offline');}).connect(),/unavailable/);
 await assert.rejects(new HttpLocalHostAdapter(undefined,async()=>new Response('{}',{headers:{'content-type':'application/json'}})).connect(),/Malformed/);
 assert.throws(()=>validateSnapshot({host:{protocolVersion:'1.0'},session:{}}),/incompatible/);
 await assert.rejects(new HttpLocalHostAdapter(undefined,async()=>new Response('x'.repeat(65537),{headers:{'content-type':'application/json'}})).connect(),/limit/);
});

test('default fetch is called without an adapter receiver in browser-compatible transport',async t=>{
 let called=false;t.mock.method(globalThis,'fetch',async function(this:unknown){assert.equal(this,undefined);called=true;return new Response('{}',{headers:{'content-type':'application/json'}});});
 await assert.rejects(new HttpLocalHostAdapter().connect(),/Malformed/);assert.equal(called,true);
});

test('protocol 1.3 accepts boolean Preview capability and rejects malformed or old capability responses',()=>{const now=new Date().toISOString();const s={host:{protocolVersion:'1.3',hostKind:'local',platform:'linux',projectRoot:'/project',capabilities:{readProject:true,writeConfig:true,build:true,preview:true,watchFiles:false}},session:{projectId:'p',displayName:'Project',projectRoot:'/project',openedAt:now,refreshedAt:now,mapping:'unknown',metadata:'unavailable',sourceState:'available',configFileState:'available',binaryState:'available'}};assert.equal(validateSnapshot(s).host.capabilities.preview,true);assert.equal(validateSnapshot({...s,host:{...s.host,capabilities:{...s.host.capabilities,preview:false}}}).host.capabilities.preview,false);assert.throws(()=>validateSnapshot({...s,host:{...s.host,capabilities:{...s.host.capabilities,preview:'yes'}}}));assert.throws(()=>validateSnapshot({...s,host:{...s.host,protocolVersion:'1.2'}}));});
