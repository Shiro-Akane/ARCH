import test from 'node:test';import assert from 'node:assert/strict';import {mkdtemp,writeFile,rm} from 'node:fs/promises';import os from 'node:os';import path from 'node:path';import {readConfig,prepareSave} from '../host/config.ts';
test('config reads exact UTF-8 including BOM, CRLF, unknown text and confines the selected path',async t=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch-config-'));t.after(()=>rm(root,{recursive:true,force:true}));const text='\ufeff# comment\r\nunknown = 2e-5\r\n\r\nnblockx1 = 4';await writeFile(path.join(root,'case.par'),text);
 const data=await readConfig(root,'case.par','p');assert.equal(data.text,text);assert.equal(data.fingerprint.size,Buffer.byteLength(text));assert.equal(data.projectId,'p');
 await assert.rejects(readConfig(root,'../escape.par','p'));await assert.rejects(readConfig(root,'file.cpp','p'));await assert.rejects(readConfig(root,undefined,'p'));
 await writeFile(path.join(root,'bad.par'),Buffer.from([255]));await assert.rejects(readConfig(root,'bad.par','p'),/UTF-8/);
});

test('save preparation rejects stale fingerprints and does not touch original bytes',async t=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch-cas-'));t.after(()=>rm(root,{recursive:true,force:true}));await writeFile(path.join(root,'a.par'),'x = 1');const first=await readConfig(root,'a.par','p');await writeFile(path.join(root,'a.par'),'x = 2');await assert.rejects(prepareSave(root,'a.par','p',first.fingerprint,'x = 3'),/changed on disk/);assert.equal((await readConfig(root,'a.par','p')).text,'x = 2');
});
