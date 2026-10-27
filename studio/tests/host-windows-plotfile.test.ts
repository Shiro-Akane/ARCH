import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,realpath,copyFile,rm,writeFile,rename,open,readFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
import os from 'node:os';
import {inspectPlotfileMetadata,readPlotfileFieldSlice} from '../host/plotfileMetadata.ts';
import {inspectPlotfileMetadataIsolated} from '../host/isolatedPlotfileMetadata.ts';
import {openProject} from '../host/project.ts';
import {holdWindowsRead} from '../host/windowsFilePublish.ts';
import {createHostServer,listenLocal} from '../host/server.ts';
import type {WindowsHostTools} from '../host/hostProcess.ts';

const native=process.platform==='win32';
function tools():WindowsHostTools{
 for(const key of ['ARCH_TEST_CMAKE','ARCH_TEST_SUPERVISOR','ARCH_TEST_PUBLISHER'])assert.ok(process.env[key],key+' required');
 return {cmake:process.env.ARCH_TEST_CMAKE!,supervisor:process.env.ARCH_TEST_SUPERVISOR!,publisher:process.env.ARCH_TEST_PUBLISHER!,path:process.env.PATH!};
}
async function fixture(t:{after:(fn:()=>Promise<void>)=>void}){
 const root=await realpath(await mkdtemp(path.join(os.tmpdir(),'arch-plot 中文 space-')));
 t.after(()=>rm(root,{recursive:true,force:true}));
 const target=path.join(root,'结果 file.h5');await copyFile(fileURLToPath(new URL('./fixtures/sod-1d.h5',import.meta.url)),target);
 return {root,target,windowsTools:tools()};
}

test('native original HDF reader and isolated transport preserve known file bytes and identity',{skip:!native},async t=>{
 const {target,windowsTools}=await fixture(t);const bytes=await readFile(target);
 const direct=await inspectPlotfileMetadata(target,{windowsTools});
 assert.equal(direct.dimension,1);assert.equal(direct.time,0.15);assert.equal(direct.renderEligible,false);
 const isolated=await inspectPlotfileMetadataIsolated(target,{windowsTools});assert.equal(isolated.file.sha256,direct.file.sha256);
 const slice=await readPlotfileFieldSlice(target,{field:'PRES',block:Math.floor(32/direct.cellShape[0]),start:[32%direct.cellShape[0]],count:[1]},{windowsTools});
 assert.deepEqual(slice.payload!.values,[0.3054751636143017]);assert.deepEqual(await readFile(target),bytes);
});
test('native isolated startup failure releases capacity for the next trusted read',{skip:!native},async t=>{
 const {target,windowsTools}=await fixture(t);
 await assert.rejects(inspectPlotfileMetadataIsolated(target),error=>(error as {code?:string}).code==='WORKER_FAILED');
 await assert.rejects(inspectPlotfileMetadataIsolated(target,{windowsTools:{...windowsTools,supervisor:'relative.exe'}}),error=>(error as {code?:string}).code==='WORKER_FAILED');
 assert.equal((await inspectPlotfileMetadataIsolated(target,{windowsTools})).dimension,1);
});
test('native HDF open holds actual file and parent identity throughout decode',{skip:!native},async t=>{
 const {root,target,windowsTools}=await fixture(t);
 await inspectPlotfileMetadata(target,{windowsTools,afterPin:async()=>{
  await assert.rejects(writeFile(target,'overwrite'));await assert.rejects(rename(root,root+'-moved'));
  await assert.rejects(rename(target,target+'.moved'));
 }});
 await writeFile(target,'released');
});
test('native HDF admission retains invalid, empty, partial and 64MiB bounds',{skip:!native},async t=>{
 const {target,windowsTools}=await fixture(t);
 await assert.rejects(inspectPlotfileMetadata(target+'.partial-ABC123',{windowsTools}),/not a published/);
 await writeFile(target,'invalid hdf5');await assert.rejects(inspectPlotfileMetadata(target,{windowsTools}));
 await writeFile(target,'');await assert.rejects(inspectPlotfileMetadata(target,{windowsTools}),/non-empty/);
 const file=await open(target,'w');try{await file.truncate(64*1024*1024+1);}finally{await file.close();}
 await assert.rejects(inspectPlotfileMetadata(target,{windowsTools}),/at most 64 MiB/);
});
test('native Host routes use trusted tools and retain request allowlists',{skip:!native},async t=>{
 const {root,target,windowsTools}=await fixture(t);
 await writeFile(path.join(root,'input.par'),'# first');
 const reader=await openProject({project:root,config:'input.par',configureProfile:{id:'io-test',sourceRoot:root,buildDirRelative:'build',generator:'Ninja',definitions:[],windowsTools}});
 assert.equal(reader.snapshot().host.capabilities.writeConfig,true);
 const server=createHostServer(reader,'http://127.0.0.1:5173');await listenLocal(server,0);
 t.after(()=>new Promise<void>((resolve,reject)=>server.close(error=>error?reject(error):resolve())));
 const address=server.address();assert.ok(address&&typeof address!=='string');
 const base='http://127.0.0.1:'+address.port,headers={Origin:'http://127.0.0.1:5173','X-ARCH-Studio':'1','X-ARCH-Protocol':'1.3','Content-Type':'application/json'};
 const projectId=reader.snapshot().session.projectId;
 const saved=await reader.readConfig();
 const save=await fetch(base+'/api/config/save',{method:'POST',headers,body:JSON.stringify({projectId,relativePath:'input.par',text:'\ufeff# saved\r\n',expectedFingerprint:saved.fingerprint})});assert.equal(save.status,200,await save.text());
 const response=await fetch(base+'/api/plotfile/audit-metadata',{method:'POST',headers,body:JSON.stringify({projectId,relativePath:path.basename(target)})});
 assert.equal(response.status,200,await response.clone().text());
 assert.equal((await response.json()).metadata.time,0.15);
 const invalid=await fetch(base+'/api/plotfile/audit-metadata',{method:'POST',headers,body:JSON.stringify({projectId,relativePath:path.basename(target),windowsTools})});assert.equal(invalid.status,400);
});

// A protected section must not silently lose its handle after the startup budget.
test('native read lease remains held beyond its 15s acquisition deadline',{skip:!native},async t=>{
 const {root,target,windowsTools}=await fixture(t);
 const lease=await holdWindowsRead(root,path.basename(target),windowsTools);
 try{await new Promise(resolve=>setTimeout(resolve,15500));await assert.rejects(writeFile(target,'overwrite'));}
 finally{await lease.close();}
 await writeFile(target,'released');
});
