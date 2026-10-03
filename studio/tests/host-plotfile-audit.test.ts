import {formatPlotfileRawValue} from '../src/host/plotfileRawValue.ts';
import h5 from 'h5wasm/node';
import {readPlotfileFieldSlice} from '../host/plotfileMetadata.ts';
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,copyFile,rm,readFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createHostServer,listenLocal} from '../host/server.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
import type {ProjectSnapshot} from '../src/host/contracts.ts';
const origin='http://127.0.0.1:5173';
const headers={Origin:origin,'X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION,'Content-Type':'application/json'};
const fixture=new URL('./fixtures/sod-1d.h5',import.meta.url).pathname;
async function withHost(run:(base:string,root:string)=>Promise<void>,changeSession=false){
 const root=await mkdtemp(join(tmpdir(),'arch-http-plt-'));
 await copyFile(fixture,join(root,'result.h5'));
 const snapshot:ProjectSnapshot={host:{protocolVersion:PROTOCOL_VERSION,hostKind:'local',platform:'linux',projectRoot:root,capabilities:{readProject:true,writeConfig:false,build:false,preview:false,watchFiles:false}},session:{projectId:'session',displayName:'project',projectRoot:root,sourceState:'unknown',configFileState:'unknown',binaryState:'unknown',mapping:'unknown',metadata:'unavailable',openedAt:new Date().toISOString(),refreshedAt:new Date().toISOString()}};
 let calls=0;
 const server=createHostServer({snapshot:()=>{calls++;return changeSession&&calls>1?{...snapshot,session:{...snapshot.session,projectId:'other'}}:snapshot;},refresh:async()=>snapshot},origin);
 try{
  await listenLocal(server,0);const address=server.address();assert.ok(address&&typeof address!=='string');
  assert.equal(address.address,'127.0.0.1');await run('http://127.0.0.1:'+address.port,root);
 }finally{await new Promise<void>(resolve=>server.close(()=>resolve()));await rm(root,{recursive:true,force:true});}
}
test('real Host metadata and bounded slice audits retain unknown science state and exact file version',async()=>{
 await withHost(async(base,root)=>{
  const before=await readFile(join(root,'result.h5'));
  const metadataResponse=await fetch(base+'/api/plotfile/audit-metadata',{method:'POST',headers,body:JSON.stringify({projectId:'session',relativePath:'result.h5'})});
  assert.equal(metadataResponse.status,200);const metadata=await metadataResponse.json();
  assert.equal(metadata.metadata.renderEligible,false);assert.equal(metadata.metadata.completion.state,'unknown');
  const request={projectId:'session',relativePath:'result.h5',expectedFileSha256:metadata.metadata.file.sha256,slice:{field:'PRES',block:Math.floor(32/metadata.metadata.cellShape[0]),start:[32%metadata.metadata.cellShape[0]],count:[1]}};
  const response=await fetch(base+'/api/plotfile/audit-slice',{method:'POST',headers,body:JSON.stringify(request)});
  assert.equal(response.status,200);const slice=await response.json();
  assert.deepEqual(slice.result.payload.values,[0.3054751636143017]);
  assert.deepEqual(slice.result.payload.linearIndices,[32]);assert.equal(slice.result.renderEligible,false);
  assert.equal(slice.result.scientificIdentity.binary,null);
  assert.equal((await fetch(base+'/api/plotfile/audit-slice',{method:'POST',headers,body:JSON.stringify({...request,expectedFileSha256:'0'.repeat(64)})})).status,400);
  assert.deepEqual(await readFile(join(root,'result.h5')),before);
 });
});
test('audit HTTP boundary rejects methods, origins, protocol, arbitrary execution and paths',async()=>{
 await withHost(async(base)=>{
  const route=base+'/api/plotfile/audit-metadata',request={projectId:'session',relativePath:'result.h5'};
  assert.equal((await fetch(route,{headers})).status,405);
  assert.equal((await fetch(route,{method:'POST',headers:{...headers,Origin:'http://evil.example'},body:JSON.stringify(request)})).status,403);
  assert.equal((await fetch(route,{method:'POST',headers:{...headers,'X-ARCH-Protocol':'old'},body:JSON.stringify(request)})).status,426);
  for(const invalid of [{...request,program:'sh'},{...request,env:{}},{...request,relativePath:'../result.h5'},{...request,projectId:'other'}])
   assert.equal((await fetch(route,{method:'POST',headers,body:JSON.stringify(invalid)})).status,400);
 });
});
test('late audit result cannot be returned under a different project session',async()=>{
 await withHost(async(base)=>{
  const response=await fetch(base+'/api/plotfile/audit-metadata',{method:'POST',headers,body:JSON.stringify({projectId:'session',relativePath:'result.h5'})});
  assert.equal(response.status,409);
 },true);
});

test('actual FP64 values keep their bit identity through worker and HTTP JSON',async()=>{
 await h5.ready;
 for(const dimension of [1,2])await withHost(async(base,root)=>{
  const path=join(root,'result.h5'),f=new h5.File(path,'w');
  const raw=new Float64Array([-0,0,Number.MIN_VALUE,-Number.MIN_VALUE,Number.MAX_VALUE,-Number.MAX_VALUE,1+Number.EPSILON,1-Number.EPSILON/2]);
  try{
   const shape=dimension===1?[1,8]:[1,2,4];
   f.create_attribute('time',0);f.create_attribute('dim',dimension);f.create_attribute('geometry','cartesian');
   const grid=f.create_group('Grid');
   for(const axis of ['x','y','z'])grid.create_dataset({name:axis,data:Float64Array.from({length:8},(_,i)=>i)});
   for(const key of ['level','morton'])grid.create_dataset({name:key,data:new Int32Array([0])});
   f.create_group('Data').create_dataset({name:'EDGE',data:raw,shape});
  }finally{f.close();}
  const before=await readFile(path),request={field:'EDGE',block:0,start:dimension===1?[0]:[0,0],count:dimension===1?[8]:[2,4]};
  const direct=await readPlotfileFieldSlice(path,request);
  assert.equal(Object.is(direct.payload!.values[0],-0),true,'HDF read must retain signed zero');
  const metadataResponse=await fetch(base+'/api/plotfile/audit-metadata',{method:'POST',headers,body:JSON.stringify({projectId:'session',relativePath:'result.h5'})});
  assert.equal(metadataResponse.status,200);const metadata=await metadataResponse.json();
  const response=await fetch(base+'/api/plotfile/audit-slice',{method:'POST',headers,body:JSON.stringify({projectId:'session',relativePath:'result.h5',expectedFileSha256:metadata.metadata.file.sha256,slice:request})});
  assert.equal(response.status,200);
  const body=await response.text(),result=JSON.parse(body),values=result.result.payload.values;
  assert.equal(Object.is(values[0],-0),true,'worker and HTTP must retain signed zero');
  const received=Float64Array.from(values);
  assert.deepEqual(new BigUint64Array(received.buffer),new BigUint64Array(raw.buffer));
  assert.equal(typeof values[0],'number','no string coercion or schema change');
  assert.equal(formatPlotfileRawValue(values[0]),'-0');
  assert.equal(formatPlotfileRawValue(values[1]),'0');
  assert.deepEqual(await readFile(path),before);
 });
});
