import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
import test from 'node:test';import assert from 'node:assert/strict';
import {createHostServer,listenLocal} from '../host/server.ts';
import type {ProjectSnapshot} from '../src/host/contracts.ts';
const snapshot:ProjectSnapshot={host:{protocolVersion:PROTOCOL_VERSION,hostKind:'local',platform:'test',projectRoot:'/project',capabilities:{readProject:true,writeConfig:false,build:false,preview:false,watchFiles:false}},session:{projectId:'id',displayName:'project',projectRoot:'/project',sourceState:'unknown',configFileState:'unknown',binaryState:'unknown',mapping:'unknown',metadata:'unavailable',openedAt:new Date().toISOString(),refreshedAt:new Date().toISOString()}};
test('loopback host skeleton, narrow routes, exact Origin and body rejection',async t=>{
 const server=createHostServer({snapshot:()=>snapshot,refresh:async()=>snapshot},'http://127.0.0.1:5173');await listenLocal(server,0);t.after(()=>new Promise<void>(r=>server.close(()=>r())));
 const address=server.address();assert.ok(address&&typeof address!=='string');assert.equal(address.address,'127.0.0.1');const base=`http://127.0.0.1:${address.port}`;
 const headers={Origin:'http://127.0.0.1:5173','X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION};
 assert.equal((await fetch(base+'/api/project',{headers})).status,200);
 assert.equal((await fetch(base+'/api/project')).status,403);
 assert.equal((await fetch(base+'/api/project',{headers:{...headers,Origin:'http://evil.example'}})).status,403);
 for(const route of ['/exec','/api/project/%2e%2e/secret','/api/project/files?path=/etc/passwd'])assert.equal((await fetch(base+route,{headers})).status,404);
 assert.equal((await fetch(base+'/shell',{headers,method:'POST'})).status,404);
 assert.equal((await fetch(base+'/api/project/refresh',{headers,method:'POST',body:JSON.stringify({command:'id'})})).status,400);
 assert.equal((await fetch(base+'/api/project?path=../secret',{headers})).status,404);
});

test('desktop-owned Host requires its launch token in addition to protocol and Origin',async t=>{
 const token='a'.repeat(64);
 const server=createHostServer({snapshot:()=>snapshot,refresh:async()=>snapshot},'http://127.0.0.1:5173',{token});
 await listenLocal(server,0);t.after(()=>new Promise<void>(r=>server.close(()=>r())));
 const address=server.address();assert.ok(address&&typeof address!=='string');
 const base='http://127.0.0.1:'+address.port;
 const headers={Origin:'http://127.0.0.1:5173','X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION};
 for(const extra of [{},{'X-ARCH-Desktop-Token':'wrong'}])assert.equal((await fetch(base+'/api/health',{headers:{...headers,...extra}})).status,403);
 const response=await fetch(base+'/api/health',{headers:{...headers,'X-ARCH-Desktop-Token':token}});
 assert.equal(response.status,200);assert.equal((await response.json()).desktopToken,token);
 assert.equal((await fetch(base+'/api/health',{headers:{...headers,Origin:'http://evil.example','X-ARCH-Desktop-Token':token}})).status,403);
});
