import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,readFileSync,writeFile} from 'node:fs';
import {promisify} from 'node:util';
import {createHash} from 'node:crypto';
import {previewFixture} from './preview-fixture.ts';
import {makeManifest,inputs,saveManifest} from '../host/buildManifest.ts';
import {WorkflowRunner} from '../host/workflow.ts';
import {createHostServer,listenLocal} from '../host/server.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
const read=promisify(readFile),write=promisify(writeFile);
const example=(n:string)=>JSON.parse(readFileSync(new URL('../../src/api/examples/local-workflow/'+n,import.meta.url),'utf8'));
async function fixture(timeout=1000){
 const f=await previewFixture('ok',timeout);
 const cap={version:'1',supported:true,workerPlatform:'linux',transport:'ndjson',maxInFlight:1,maxRequests:256,maxRequestBytes:8388608,maxResponseBytes:9437184,maxConfigBytes:1048576,heavyRequestWallSeconds:360,meshRequestWallSeconds:45};
 const caps=example('capabilities.json');caps.extensions.session=cap;
 const script=`#!${process.execPath}
const fs=require('node:fs'),crypto=require('node:crypto');
if(process.argv[2]==='--list-cases'){console.log(JSON.stringify(${JSON.stringify(example('registered-cases.json'))}));process.exit(0);}
if(process.argv[2]==='--preview-capabilities'){console.log(JSON.stringify(${JSON.stringify(caps)}));process.exit(0);}
if(process.argv[2]!=='--preview-session')process.exit(2);
console.log(JSON.stringify({kind:'preview-session-ready',version:'1',sequence:0,capability:${JSON.stringify(cap)}}));
let sequence=0;
require('node:readline').createInterface({input:process.stdin}).on('line',line=>{
 const r=JSON.parse(line);fs.appendFileSync('workflow-requests.log',JSON.stringify({pid:process.pid,...r})+'\\n');
 const identity={requestId:r.requestId,caseId:r.caseId,configRevision:crypto.createHash('sha256').update(r.configText).digest('hex')};
 const envelope={version:'1',sequence:++sequence,command:r.command,identity};
 console.log(JSON.stringify({...envelope,kind:'preview-session-progress',stage:'setup',elapsedMilliseconds:0}));
 if(r.configText.includes('hang'))return;
 const response=r.command==='--preview-amr'?${JSON.stringify(example('sod-limited.json'))}:r.command==='--amr-resources'?${JSON.stringify(example('sod-resources.json'))}:${JSON.stringify(example('case-inspection-sod.json'))};
 response.identity={...identity};if(r.configText.includes('wrong'))response.identity.configRevision='wrong';
 setTimeout(()=>console.log(JSON.stringify({...envelope,kind:'preview-session-result',exitCode:0,elapsedMilliseconds:50,stages:[],resources:{resultReused:false,tableLoads:0,tableHits:0,retainedTables:0,fileContentMatches:0,fileHashes:0,retainedFileBytes:0},response})),100);
});
`;
 await write(f.root+'/build/bin/ARCH',script,{mode:0o755});
 await saveManifest(f.p,await makeManifest(f.p,'p','workflow-build',new Date().toISOString(),await inputs(f.p),undefined,{}));await f.build.initialize();
 const workflow=new WorkflowRunner(f.preview);f.preview.externalBusy=()=>workflow.isActive();f.build.executionBlocked=()=>f.preview.isActive()||workflow.isActive();
 const request=(text='x_pos=.4',operation:'inspect-case'|'preview-amr'|'amr-resources'='inspect-case')=>({projectId:'p',caseId:'Sod',operation,configText:text,configRevision:createHash('sha256').update(text).digest('hex')});
 const finish=async()=>{const until=Date.now()+4000;while(workflow.isActive()&&Date.now()<until)await new Promise(r=>setTimeout(r,5));assert.equal(workflow.isActive(),false);return workflow.snapshot();};
 const setup=async()=>{const until=Date.now()+2000;while(workflow.snapshot().stage!=='setup'&&Date.now()<until)await new Promise(r=>setTimeout(r,5));assert.equal(workflow.snapshot().stage,'setup');};
 return {...f,workflow,request,finish,setup,cleanup:async()=>{await f.preview.shutdown();await f.cleanup();}};
}
test('B requests reuse the same worker, preserve limited, and reject stale nested identity',async()=>{
 const f=await fixture();try{
  assert.equal((await f.workflow.discovery()).cases.length,11);
  await f.workflow.start(f.request());const first=await f.finish();assert.equal(first.state,'succeeded');
  await f.workflow.start(f.request('x_pos=.4','preview-amr'));const mesh=await f.finish();assert.equal(mesh.result?.core.status,'limited');
  await f.workflow.start(f.request('wrong'));const bad=await f.finish();assert.equal(bad.state,'failed');assert.equal(bad.result?.identity.requestId,mesh.result?.identity.requestId);
  const rows=(await read(f.root+'/workflow-requests.log','utf8')).trim().split('\n').map(s=>JSON.parse(s));assert.equal(new Set(rows.map(r=>r.pid)).size,1);
 }finally{await f.cleanup();}
});
test('B cancellation reaps owned worker and blocks field/Build; next command starts new process',async()=>{
 const f=await fixture(3000);try{
  const ack=await f.workflow.start(f.request('hang'));await f.setup();
  const first=JSON.parse((await read(f.root+'/workflow-requests.log','utf8')).trim());
  await assert.rejects(f.preview.start(f.request()),/workflow is active/);
  await assert.rejects(f.build.start('p',f.p.id),/Preview is active/);
  await assert.rejects(f.workflow.cancel('other'),/matching/);
  await f.workflow.cancel(ack.identity.requestId);assert.equal(f.workflow.snapshot().state,'cancelled');assert.throws(()=>process.kill(first.pid,0),/ESRCH/);
  await f.workflow.start(f.request());assert.equal((await f.finish()).state,'succeeded');
  const rows=(await read(f.root+'/workflow-requests.log','utf8')).trim().split('\n').map(s=>JSON.parse(s));assert.notEqual(rows[1].pid,first.pid);
 }finally{await f.cleanup();}
});
test('B timeout and build change reject result without replacing last success',async()=>{
 const f=await fixture(250);try{
  await f.workflow.start(f.request());const saved=(await f.finish()).result;
  await f.workflow.start(f.request('hang'));const timed=await f.finish();assert.equal(timed.state,'failed');assert.match(timed.error!,/timed out/);assert.equal(timed.result?.identity.requestId,saved?.identity.requestId);
  await f.workflow.start(f.request());await f.setup();await write(f.root+'/case.cpp','changed');
  const changed=await f.finish();assert.equal(changed.state,'failed');assert.equal(changed.result?.identity.requestId,saved?.identity.requestId);
 }finally{await f.cleanup();}
});
test('B rejects arbitrary command fields, unknown case and out-of-contract budgets',async()=>{
 const f=await fixture();try{
  for(const extra of [{program:'sh'},{args:['x']},{cwd:'/tmp'},{env:{}},{shell:true},{projectId:'other'},{configRevision:'wrong'},{operation:'simulation'},{caseId:'unregistered'},{meshMaxBlocks:1}])await assert.rejects(f.workflow.start({...f.request(),...extra} as never));
  for(const extra of [{meshMaxBlocks:0},{meshMaxBlocks:1025},{meshMemoryMiB:257},{meshMemoryMiB:15},{meshMaxBlocks:1.5}])await assert.rejects(f.workflow.start({...f.request('x_pos=.4','preview-amr'),...extra}));
  assert.equal(f.preview.isActive(),false);
 }finally{await f.cleanup();}
});
test('B HTTP endpoints keep origin/protocol and semantic request barriers',async()=>{
 const f=await fixture();const server=createHostServer({workflow:f.workflow,snapshot:()=>({session:{projectId:'p'},host:{}}) as never,refresh:async()=>({}) as never},'http://127.0.0.1:4190');
 try{
  await listenLocal(server,0);const port=(server.address() as {port:number}).port,url='http://127.0.0.1:'+port;
  const headers={'Origin':'http://127.0.0.1:4190','X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION,'Content-Type':'application/json'};
  assert.equal((await fetch(url+'/api/cases',{headers})).status,200);
  assert.equal((await fetch(url+'/api/cases',{headers:{...headers,Origin:'http://evil'}})).status,403);
  assert.equal((await fetch(url+'/api/workflow',{method:'POST',headers,body:JSON.stringify({...f.request(),program:'sh'})})).status,400);
  const response=await fetch(url+'/api/workflow',{method:'POST',headers,body:JSON.stringify(f.request())});assert.equal(response.status,202);await f.finish();
 }finally{await new Promise<void>(r=>server.close(()=>r()));await f.cleanup();}
});
