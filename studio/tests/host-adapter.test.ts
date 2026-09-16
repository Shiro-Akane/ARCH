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
