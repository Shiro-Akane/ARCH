import test from 'node:test';
import assert from 'node:assert/strict';
import {writeFile,readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {previewFixture,corePayload,previewFinished} from './preview-fixture.ts';
import {inputs,makeManifest,saveManifest} from '../host/buildManifest.ts';
const capability={version:'1',supported:true,workerPlatform:'linux',transport:'ndjson',maxInFlight:1,
 maxRequests:256,maxRequestBytes:8388608,maxResponseBytes:9437184,maxConfigBytes:1048576,
 heavyRequestWallSeconds:360,meshRequestWallSeconds:45};
async function fixture(){
 const f=await previewFixture('ok',2000);
 const caps={schemaVersion:'1.0',kind:'preview-capabilities',cases:['Sod'],dimensions:[1],extensions:{session:capability}};
 const script=`#!${process.execPath}
const crypto=require('node:crypto'),fs=require('node:fs');
if(process.argv[2]==='--preview-capabilities'){console.log(JSON.stringify(${JSON.stringify(caps)}));process.exit(0);}
if(process.argv[2]!=='--preview-session')process.exit(2);
let sequence=0;
const emit=v=>console.log(JSON.stringify(v));
emit({kind:'preview-session-ready',version:'1',sequence:0,capability:${JSON.stringify(capability)}});
require('node:readline').createInterface({input:process.stdin}).on('line',line=>{
 const r=JSON.parse(line),identity={requestId:r.requestId,caseId:r.caseId,configRevision:crypto.createHash('sha256').update(r.configText).digest('hex')};
 fs.appendFileSync('requests.log',JSON.stringify({pid:process.pid,...r})+'\\n');
 const base={version:'1',sequence:++sequence,identity,command:r.command};
 emit({...base,kind:'preview-session-progress',stage:'setup',elapsedMilliseconds:1});
 const response=${JSON.stringify(corePayload())};response.identity=identity;
 if(r.configText.includes('failure')){response.status='error';response.data=null;response.diagnostics=[{severity:'error',code:'FAIL',message:'init failure'}];}
 setTimeout(()=>emit({...base,kind:'preview-session-result',exitCode:response.status==='ok'?0:3,elapsedMilliseconds:150,
 stages:[{stage:'setup',milliseconds:150}],resources:{resultReused:false,tableLoads:0,tableHits:0,retainedTables:0,fileContentMatches:0,fileHashes:0,retainedFileBytes:0},response}),150);
});
`;
 await writeFile(f.root+'/build/bin/ARCH',script,{mode:0o755});
 await saveManifest(f.p,await makeManifest(f.p,'p','session-build',new Date().toISOString(),await inputs(f.p),undefined,{}));
 await f.build.initialize();
 const request=(text:string)=>({...f.request,configText:text,configRevision:createHash('sha256').update(text).digest('hex')});
 const waitActive=async()=>{
  const until=Date.now()+2000;
  while(f.preview.snapshot().session?.stage!=='setup'&&Date.now()<until)await new Promise(r=>setTimeout(r,5));
  assert.equal(f.preview.snapshot().session?.stage,'setup');
 };
 return {...f,request,waitActive,cleanup:async()=>{await f.preview.shutdown();await f.cleanup();}};
}
test('Host collapses edits to latest pending; obsolete active success never replaces saved result',async()=>{
 const f=await fixture();try{
  await f.preview.start(f.request('x_pos=.2'));const saved=await previewFinished(f.preview);
  const a=await f.preview.start(f.request('x_pos=.3'));await f.waitActive();
  const b=await f.preview.start(f.request('x_pos=.31'));
  await f.preview.start(f.request('x_pos=.32'));
  const e=await f.preview.start(f.request('x_pos=.33'));
  const status=f.preview.snapshot();
  assert.equal(status.queue?.activeRequestId,a.requestId);assert.equal(status.queue?.pendingRequestId,e.requestId);
  assert.equal(status.queue?.replacedPending,2);assert.equal(status.result?.identity.requestId,saved.result?.identity.requestId);
  assert.notEqual(b.requestId,e.requestId);
  const observed:string[]=[];
  const timer=setInterval(()=>{const id=f.preview.snapshot().result?.identity.requestId;if(id)observed.push(id);},2);
  const done=await previewFinished(f.preview);clearInterval(timer);
  assert.equal(done.state,'succeeded');assert.equal(done.result?.identity.requestId,e.requestId);
  assert.ok(!observed.includes(a.requestId));
  const calls=(await readFile(f.root+'/requests.log','utf8')).trim().split('\n').map(JSON.parse);
  assert.deepEqual(calls.map(r=>r.configText),['x_pos=.2','x_pos=.3','x_pos=.33']);
  assert.equal(new Set(calls.map(r=>r.pid)).size,1);
  assert.ok(done.timing!.hostQueueMilliseconds>0);
 }finally{await f.cleanup();}
});
test('cancel latest pending owns the active session and drops all pending input',async()=>{
 const f=await fixture();try{
  await f.preview.start(f.request('x_pos=.2'));const saved=await previewFinished(f.preview);
  await f.preview.start(f.request('x_pos=.3'));await f.waitActive();
  const pending=await f.preview.start(f.request('x_pos=.4'));
  f.preview.cancel(pending.requestId);
  const ended=await previewFinished(f.preview);
  assert.equal(ended.state,'cancelled');assert.equal(ended.queue,undefined);
  assert.equal(ended.result?.identity.requestId,saved.result?.identity.requestId);
  await f.preview.start(f.request('x_pos=.5'));const next=await previewFinished(f.preview);
  assert.equal(next.state,'succeeded');assert.notEqual(next.session?.processToken,saved.session?.processToken);
  const calls=(await readFile(f.root+'/requests.log','utf8')).trim().split('\n').map(JSON.parse);
  assert.deepEqual(calls.map(r=>r.configText),['x_pos=.2','x_pos=.3','x_pos=.5']);
 }finally{await f.cleanup();}
});
test('failed latest request retains last success and changed build prevents queued execution',async()=>{
 const f=await fixture();try{
  await f.preview.start(f.request('x_pos=.2'));const saved=await previewFinished(f.preview);
  await f.preview.start(f.request('failure'));const failed=await previewFinished(f.preview);
  assert.equal(failed.state,'failed');assert.equal(failed.result?.identity.requestId,saved.result?.identity.requestId);
  await f.preview.start(f.request('x_pos=.3'));await f.waitActive();
  await f.preview.start(f.request('x_pos=.4'));
  await writeFile(f.root+'/case.cpp','changed');
  const ended=await previewFinished(f.preview);
  assert.equal(ended.state,'failed');assert.equal(ended.result?.identity.requestId,saved.result?.identity.requestId);
  const calls=(await readFile(f.root+'/requests.log','utf8')).trim().split('\n').map(JSON.parse);
  assert.ok(!calls.some(r=>r.configText==='x_pos=.4'));
 }finally{await f.cleanup();}
});

test('a controlled Build retires and reaps the idle session before compiling',async()=>{
 const f=await fixture();try{
  await f.preview.start(f.request('x_pos=.2'));await previewFinished(f.preview);
  const call=JSON.parse((await readFile(f.root+'/requests.log','utf8')).trim());
  assert.doesNotThrow(()=>process.kill(call.pid,0));
  await f.build.start('p',f.p.id);
  assert.throws(()=>process.kill(call.pid,0),/ESRCH/);
  const until=Date.now()+3000;
  while(f.build.isActive()&&Date.now()<until)await new Promise(resolve=>setTimeout(resolve,10));
  assert.equal(f.build.isActive(),false);
  // This fixture deliberately has no real build system. The failed build must not restore a session.
  assert.equal(f.build.snapshot().state,'failed');
 }finally{await f.cleanup();}
});
