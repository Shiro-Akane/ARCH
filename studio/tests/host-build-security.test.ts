import test from 'node:test';import assert from 'node:assert/strict';import {mkdir,rm,symlink,writeFile} from 'node:fs/promises';import {EventEmitter} from 'node:events';import {PassThrough} from 'node:stream';
import {fixture,fakeSpawn,finished} from './build-fixture.ts';import {BuildRunner} from '../host/buildRunner.ts';import {validateProfile} from '../host/buildProfile.ts';import {openProject} from '../host/project.ts';import {createHostServer,listenLocal} from '../host/server.ts';
test('HTTP build rejects injected fields, unknown profile, duplicate requests and stale event IDs',async()=>{const {root,p}=await fixture();await mkdir(root+'/studio');const reader=await openProject({project:root});const projectId=reader.snapshot().session.projectId;const build=new BuildRunner(root,projectId,p,{spawn:fakeSpawn(root,0,true,150)});await build.initialize();const server=createHostServer({...reader,build},'http://127.0.0.1:5173');await listenLocal(server,0);const address=server.address();assert.ok(address&&typeof address!=='string');const base=`http://127.0.0.1:${address.port}`;const headers={Origin:'http://127.0.0.1:5173','X-ARCH-Studio':'1','X-ARCH-Protocol':'1.2','Content-Type':'application/json'};const post=(v:unknown)=>fetch(base+'/api/build',{method:'POST',headers,body:JSON.stringify(v)});try{
 for(const key of ['program','command','args','cwd','env','shell','script','binary'])assert.equal((await post({projectId,profileId:p.id,[key]:'untrusted'})).status,400);
 assert.equal((await post({projectId,profileId:'unknown'})).status,400);
 const started=await post({projectId,profileId:p.id});assert.equal(started.status,202);const id=(await started.json()).buildId;
 assert.equal((await post({projectId,profileId:p.id})).status,409);
 await finished(build);assert.equal((await fetch(base+'/api/build/'+id+'/events',{headers})).status,200);assert.equal((await fetch(base+'/api/build/00000000-0000-0000-0000-000000000000/events',{headers})).status,404);
 assert.equal((await fetch(base+'/api/build/status?cwd=/tmp',{headers})).status,404);
 assert.equal((await fetch(base+'/api/build/status',{headers:{...headers,'X-ARCH-Protocol':'1.1'}})).status,426);
 }finally{await new Promise<void>(resolve=>server.close(()=>resolve()));await rm(root,{recursive:true,force:true});}});
test('spawn failure, nonzero exit and missing target preserve inputs and prior records',async()=>{const {root,p}=await fixture();await mkdir(root+'/studio');try{
 const failedSpawn=(()=>{const c=new EventEmitter() as EventEmitter&{stdout:PassThrough;stderr:PassThrough};c.stdout=new PassThrough();c.stderr=new PassThrough();queueMicrotask(()=>c.emit('error',new Error('ENOENT')));return c;}) as never;
 const r=new BuildRunner(root,'p',p,{spawn:failedSpawn});await r.start('p',p.id);assert.equal((await finished(r)).state,'failed');assert.match(r.snapshot().latestResult!.error!,/could not start/);
 const fail=new BuildRunner(root,'p',{...p,target:'missing-target'},{spawn:fakeSpawn(root,1,false)});await fail.start('p',p.id);assert.equal((await finished(fail)).state,'failed');
 await symlink('/tmp',root+'/outside');await assert.rejects(validateProfile(root,{...p,buildDirRelative:'outside'}));await writeFile(root+'/build/CMakeCache.txt','CMAKE_HOME_DIRECTORY:INTERNAL=/wrong\n');await assert.rejects(validateProfile(root,p),/source root/);
 }finally{await rm(root,{recursive:true,force:true});}});
