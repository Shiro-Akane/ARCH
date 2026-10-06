import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,writeFile,readFile,rm,symlink,mkdir} from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {openProject} from '../host/project.ts';
import {createHostServer,listenLocal} from '../host/server.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';

test('opening an existing config associates exact bytes without writing and keeps the selection on failure',async t=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch-open-config-'));
 const outside=await mkdtemp(path.join(os.tmpdir(),'arch-outside-config-'));
 t.after(()=>Promise.all([rm(root,{recursive:true,force:true}),rm(outside,{recursive:true,force:true})]));
 await writeFile(path.join(root,'old.par'),'x = 1\n');
 await mkdir(path.join(root,'space dir'));
 const text='\ufeff# preserve\r\nunknown = 2e-5\r\n';
 await writeFile(path.join(root,'space dir/new.par'),text);
 await writeFile(path.join(outside,'escape.par'),'x = 9');
 await symlink(path.join(outside,'escape.par'),path.join(root,'escape.par'));
 const reader=await openProject({project:root,config:'old.par'});
 const projectId=reader.snapshot().session.projectId;
 const opened=await reader.openConfig({projectId,relativePath:'space dir/new.par'});
 assert.equal(opened.text,text);
 assert.equal(opened.project.session.parameterFile?.relativePath,'space dir/new.par');
 assert.equal(opened.project.session.parameterFile?.sha256,opened.fingerprint.sha256);
 assert.equal((await reader.readConfig()).text,text);
 assert.equal(await readFile(path.join(root,'old.par'),'utf8'),'x = 1\n');
 assert.equal(await readFile(path.join(root,'space dir/new.par'),'utf8'),text);
 for(const relativePath of ['missing.par','../escape.par','escape.par','new.cpp']){
  await assert.rejects(reader.openConfig({projectId,relativePath}));
  assert.equal(reader.snapshot().session.parameterFile?.relativePath,'space dir/new.par');
 }
 await assert.rejects(reader.openConfig({projectId:'stale',relativePath:'old.par'}));
 assert.equal((await reader.readConfig()).relativePath,'space dir/new.par');
 const changed=await reader.saveConfig({projectId,relativePath:'space dir/new.par',text:text+'x = 3\n',expectedFingerprint:opened.fingerprint});
 assert.equal(changed.text,text+'x = 3\n');
 await assert.rejects(reader.saveConfig({projectId,relativePath:'old.par',text:'no overwrite',expectedFingerprint:opened.fingerprint}));
});

test('config open HTTP rejects commands, stale session and active operations before changing association',async t=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch-open-api-'));
 t.after(()=>rm(root,{recursive:true,force:true}));
 await writeFile(path.join(root,'old.par'),'x = 1');
 await writeFile(path.join(root,'new.par'),'x = 2');
 const reader=await openProject({project:root,config:'old.par'});
 let busy=false;
 const server=createHostServer({...reader,preview:{isActive:()=>busy} as never},'http://127.0.0.1:5173');
 await listenLocal(server,0);
 t.after(()=>new Promise<void>(resolve=>server.close(()=>resolve())));
 const address=server.address();assert.ok(address&&typeof address!=='string');
 const url='http://127.0.0.1:'+address.port+'/api/config/open';
 const headers={Origin:'http://127.0.0.1:5173','X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION,'Content-Type':'application/json'};
 const request={projectId:reader.snapshot().session.projectId,relativePath:'new.par'};
 const post=(payload:unknown)=>fetch(url,{method:'POST',headers,body:JSON.stringify(payload)});
 assert.equal((await post({...request,command:'id'})).status,400);
 assert.equal((await post({...request,projectId:'stale'})).status,400);
 assert.equal((await fetch(url,{method:'POST',headers:{...headers,Origin:'http://invalid'},body:JSON.stringify(request)})).status,403);
 assert.equal((await fetch(url,{headers})).status,405);
 busy=true;assert.equal((await post(request)).status,409);
 assert.equal(reader.snapshot().session.parameterFile?.relativePath,'old.par');
 busy=false;
 const response=await post(request);assert.equal(response.status,200);
 const data=await response.json();assert.equal(data.project.session.parameterFile.relativePath,'new.par');
 assert.equal(data.text,'x = 2');
});
