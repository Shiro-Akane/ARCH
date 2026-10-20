import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
import test from 'node:test';import assert from 'node:assert/strict';import {mkdtemp,writeFile,readFile,rm} from 'node:fs/promises';import os from 'node:os';import path from 'node:path';import {openProject} from '../host/project.ts';import {createHostServer,listenLocal} from '../host/server.ts';
test('real HTTP lifecycle: strict protocol/body, compare-and-save, serialized concurrency, Save As current identity',async t=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch-api-'));t.after(()=>rm(root,{recursive:true,force:true}));await writeFile(path.join(root,'a.par'),'# keep\r\nnblockx1 = 4\r\nunknown = alpha\r\n');const project=await openProject({project:root,config:'a.par'});const server=createHostServer(project,'http://127.0.0.1:5173');await listenLocal(server,0);t.after(()=>new Promise<void>(r=>server.close(()=>r())));const address=server.address();assert.ok(address&&typeof address!=='string');const base=`http://127.0.0.1:${address.port}`;const headers={Origin:'http://127.0.0.1:5173','X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION};
 const read=await (await fetch(base+'/api/config',{headers})).json();assert.equal(read.text,await readFile(path.join(root,'a.par'),'utf8'));
 const post=(route:string,body:unknown)=>fetch(base+route,{method:'POST',headers:{...headers,'Content-Type':'application/json'},body:JSON.stringify(body)});
 assert.equal((await fetch(base+'/api/config',{headers:{...headers,'X-ARCH-Protocol':'1.0'}})).status,426);
 assert.equal((await post('/api/config/save',{command:'rm',text:'x'})).status,400);
 const request={projectId:read.projectId,relativePath:'a.par',expectedFingerprint:read.fingerprint,text:read.text.replace('= 4','= 8')};
 const results=await Promise.all([post('/api/config/save',request),post('/api/config/save',request)]);assert.deepEqual(results.map(r=>r.status).sort(),[200,409]);assert.equal(await readFile(path.join(root,'a.par'),'utf8'),request.text);
 assert.equal((await post('/api/config/save-as',{projectId:read.projectId,destinationRelativePath:'../escape.par',text:'bad'})).status,400);
 assert.equal((await post('/api/config/save-as',{projectId:read.projectId,destinationRelativePath:'huge.par',text:'a'.repeat(1024*1024)})).status,413);
 const saved=await post('/api/config/save-as',{projectId:read.projectId,destinationRelativePath:'new.par',text:request.text});assert.equal(saved.status,200);assert.equal((await saved.json()).project.session.parameterFile.relativePath,'new.par');
 assert.equal((await post('/api/config/save-as',{projectId:read.projectId,destinationRelativePath:'new.par',text:'bad'})).status,409);
 assert.equal(await readFile(path.join(root,'a.par'),'utf8'),request.text);
});
