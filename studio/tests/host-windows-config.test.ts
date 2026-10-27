import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,realpath,writeFile,readFile,readdir,rename,rm,unlink} from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {atomicSave,publishConfig} from '../host/atomicConfig.ts';
import {readConfig,ConfigError} from '../host/config.ts';
import type {WindowsHostTools} from '../host/hostProcess.ts';

const native=process.platform==='win32';
function tools():WindowsHostTools {
 for(const name of ['ARCH_TEST_CMAKE','ARCH_TEST_SUPERVISOR','ARCH_TEST_PUBLISHER'])assert.ok(process.env[name],name+' required for native qualification');
 return {cmake:process.env.ARCH_TEST_CMAKE!,supervisor:process.env.ARCH_TEST_SUPERVISOR!,publisher:process.env.ARCH_TEST_PUBLISHER!,path:process.env.PATH!};
}
async function fixture(t:{after:(fn:()=>Promise<void>)=>void}){
 const root=await realpath(await mkdtemp(path.join(os.tmpdir(),'arch-config 中文 space-')));
 t.after(()=>rm(root,{recursive:true,force:true}));
 await writeFile(path.join(root,'输入.par'),'# original\r\nx = 1\r\n');
 return {root,first:await readConfig(root,'输入.par','p'),windowsTools:tools()};
}
async function noTemp(root:string){assert.deepEqual((await readdir(root)).filter(x=>x.startsWith('.arch-config-')),[]);}

test('native Save and Save As preserve exact UTF-8, BOM, CRLF and empty text',{skip:!native},async t=>{
 const {root,first,windowsTools}=await fixture(t);const text='\ufeff# 注释\r\nx = 3\r\nunknown = keep';
 const saved=await atomicSave(root,'输入.par','p',text,first.fingerprint,{windowsTools});
 assert.equal(saved.text,text);assert.equal(await readFile(path.join(root,'输入.par'),'utf8'),text);
 const copy=await publishConfig(root,'另存 file.par','p',text,undefined,true,{windowsTools});assert.equal(copy.text,text);
 assert.equal((await publishConfig(root,'empty.par','p','',undefined,true,{windowsTools})).text,'');await noTemp(root);
});
test('native Save As never overwrites and rejects stale Save fingerprints',{skip:!native},async t=>{
 const {root,first,windowsTools}=await fixture(t);
 await assert.rejects(publishConfig(root,'输入.par','p','overwrite',undefined,true,{windowsTools}),e=>e instanceof ConfigError&&e.info.code==='destination-exists');
 await writeFile(path.join(root,'输入.par'),'external');
 await assert.rejects(atomicSave(root,'输入.par','p','overwrite',first.fingerprint,{windowsTools}),e=>e instanceof ConfigError&&e.info.code==='changed-externally');
 assert.equal(await readFile(path.join(root,'输入.par'),'utf8'),'external');await noTemp(root);
});
test('native before-write and precommit external edits reject and clean only owned temp',{skip:!native},async t=>{
 const {root,windowsTools}=await fixture(t);
 for(const hook of ['beforeWrite','beforeCommit'] as const){
  const first=await readConfig(root,'输入.par','p');
  await assert.rejects(atomicSave(root,'输入.par','p','new',first.fingerprint,{windowsTools,[hook]:()=>writeFile(path.join(root,'输入.par'),hook)}),e=>e instanceof ConfigError&&e.info.code==='changed-externally');
  assert.equal(await readFile(path.join(root,'输入.par'),'utf8'),hook);await noTemp(root);
 }
});
test('native temp and pinned parent resist replacement while original remains readable',{skip:!native},async t=>{
 const {root,first,windowsTools}=await fixture(t);
 await atomicSave(root,'输入.par','p','new',first.fingerprint,{windowsTools,beforeWrite:async()=>{
  const names=(await readdir(root)).filter(x=>x.startsWith('.arch-config-'));assert.equal(names.length,1);
  const temp=path.join(root,names[0]);
  await assert.rejects(writeFile(temp,'redirect'));await assert.rejects(unlink(temp));await assert.rejects(rename(root,root+'-moved'));
  assert.equal((await readConfig(root,'输入.par','p')).text,first.text);
 }});assert.equal((await readConfig(root,'输入.par','p')).text,'new');await noTemp(root);
});
test('native failures preserve original and postcommit drift is reported truthfully',{skip:!native},async t=>{
 const {root,first,windowsTools}=await fixture(t);
 await assert.rejects(atomicSave(root,'输入.par','p','new',first.fingerprint,{windowsTools,beforeCommit:async()=>{throw new ConfigError('write-failed','injected');}}),/injected/);
 assert.equal((await readConfig(root,'输入.par','p')).text,first.text);await noTemp(root);
 await assert.rejects(atomicSave(root,'输入.par','p','new',first.fingerprint,{windowsTools,afterCommit:()=>writeFile(path.join(root,'输入.par'),'external after commit')}),e=>e instanceof ConfigError&&e.info.code==='changed-externally'&&!e.message.includes('original preserved'));
 assert.equal((await readConfig(root,'输入.par','p')).text,'external after commit');await noTemp(root);
});
