import test from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {PreviewSession, sessionCapability} from '../host/previewSession.ts';

const capability={version:'1',supported:true,workerPlatform:'linux',transport:'ndjson',maxInFlight:1,
 maxRequests:2,maxRequestBytes:8388608,maxResponseBytes:9437184,maxConfigBytes:1048576,
 heavyRequestWallSeconds:360,meshRequestWallSeconds:45};
const request={command:'--preview',caseId:'Sod',requestId:'one',configText:'x_pos=.3\n',samples:2};
const script=`
const {createHash}=require('node:crypto');
const readline=require('node:readline');
const mode=process.argv[1],cap=JSON.parse(process.argv[2]);let sequence=0;
const emit=v=>process.stdout.write(JSON.stringify(v)+'\\n');
if(mode==='ignore-term')process.on('SIGTERM',()=>{});
emit({kind:'preview-session-ready',version:'1',sequence:0,capability:cap});
readline.createInterface({input:process.stdin}).on('line',line=>{
 const r=JSON.parse(line);sequence++;
 const identity={requestId:r.requestId,caseId:r.caseId,configRevision:createHash('sha256').update(r.configText).digest('hex')};
 const base={version:'1',sequence,identity,command:r.command};
 emit({...base,kind:'preview-session-progress',stage:'complete',elapsedMilliseconds:1});
 if(mode==='hang'||mode==='ignore-term')return;
 if(mode==='error'){emit({...base,kind:'preview-session-error',code:'INVALID_SESSION_REQUEST',message:'bad config',fatal:false});return;}
 if(mode==='wrong')base.identity.requestId='other';
 if(mode==='sequence')base.sequence++;
 if(mode==='huge'){process.stdout.write('x'.repeat(2048));return;}
 if(mode==='utf8'){process.stdout.write(Buffer.from([0xff,10]));return;}
 if(mode==='truncated'){process.stdout.write('{"kind":');process.exit(0);return;}
 const result={...base,kind:'preview-session-result',exitCode:0,elapsedMilliseconds:2,
 stages:[{stage:'complete',milliseconds:1}],
 resources:{resultReused:false,tableLoads:0,tableHits:0,retainedTables:0,fileContentMatches:0,fileHashes:0,retainedFileBytes:0},
 response:{status:'ok',message:'温度',identity}};
 const send=()=>{
  if(mode==='fragment'){
   const b=Buffer.from(JSON.stringify(result)+'\\n'),i=b.indexOf(Buffer.from('温'))+1;
   process.stdout.write(b.subarray(0,i));setTimeout(()=>process.stdout.write(b.subarray(i)),5);
  }else emit(result);
  if(sequence===cap.maxRequests){emit({kind:'preview-session-closed',version:'1',sequence,reason:'request-limit'});process.exit(0);}
 };
 if(mode==='delay')setTimeout(send,60);else send();
});
`;
function session(mode='ok',overrides={}){
 const cap={...capability,...overrides};
 return new PreviewSession({binary:'/approved/ARCH',cwd:process.cwd(),capability:sessionCapability({extensions:{session:cap}}),
  spawn:(binary,args,options)=>{
   assert.equal(binary,'/approved/ARCH');assert.deepEqual(args,['--preview-session']);assert.equal(options.shell,false);
   return spawn(process.execPath,['-e',script,mode,JSON.stringify(cap)],options);
  },readyTimeoutMs:2000,timeoutMs:200,graceMs:20});
}

test('session capability is opt-in and bounded; legacy remains available',()=>{
 assert.equal(sessionCapability({}),undefined);
 assert.equal(sessionCapability({extensions:{session:{supported:false}}}),undefined);
 assert.equal(sessionCapability({extensions:{session:capability}}).heavyRequestWallSeconds,360);
 assert.throws(()=>sessionCapability({extensions:{session:{...capability,maxInFlight:2}}}));
 assert.throws(()=>sessionCapability({extensions:{session:{...capability,maxResponseBytes:1e12}}}));
});
test('fragmented UTF-8 survives; complete progress is not final success',async()=>{
 const s=session('fragment');
 try{
  let progress=0;
  const result=await s.request(request,e=>{assert.equal(e.stage,'complete');progress++;});
  assert.equal(result.response.message,'温度');assert.equal(progress,1);assert.equal(result.sequence,1);
  assert.equal(result.processToken,s.processToken);assert.ok(result.transportParseMilliseconds>=0);
 }finally{await s.terminate();}
});
test('one process serves successive requests and request-limit recycles normally',async()=>{
 const s=session();
 try{
  const first=await s.request(request);
  const second=await s.request({...request,requestId:'two',configText:'x_pos=.4'});
  assert.equal(first.processToken,second.processToken);assert.equal(second.sequence,2);
  assert.equal(second.response.identity.requestId,'two');
  await s.closed;assert.equal(s.reusable,false);
 }finally{await s.terminate();}
});
test('transport never writes a second active request',async()=>{
 const s=session('delay');
 try{
  const active=s.request(request);await s.ready;
  await assert.rejects(s.request({...request,requestId:'two'}),/busy/);
  await active;
 }finally{await s.terminate();}
});
for(const [mode,message] of [['wrong',/identity/],['sequence',/sequence/],['utf8',/encoded|encoding/],['truncated',/Truncated/],['huge',/byte limit/]]){
 test('reject and reap '+mode+' response',async()=>{
  const s=session(mode,mode==='huge'?{maxResponseBytes:1024}:{});
  try{await assert.rejects(s.request(request),message);await s.closed;assert.equal(s.reusable,false);}
  finally{await s.terminate();}
 });
}
test('recoverable input error consumes a sequence but keeps process available',async()=>{
 const s=session('error');
 try{
  await assert.rejects(s.request(request),/bad config/);assert.equal(s.reusable,true);
  await assert.rejects(s.request({...request,requestId:'two'}),/bad config/);
 }finally{await s.terminate();}
});
test('timeout does not extend on progress and kills a TERM-resistant process',async()=>{
 const s=session('ignore-term'),start=performance.now();
 await assert.rejects(s.request(request),/timed out/);
 await s.closed;
 assert.ok(performance.now()-start<2000);assert.equal(s.reusable,false);
 assert.throws(()=>process.kill(s.pid,0),/ESRCH/);
});
test('explicit cancellation rejects active request and reaps the process',async()=>{
 const s=session('hang');const pending=s.request(request);
 const rejected=assert.rejects(pending,/cancelled/);
 await s.ready;await s.terminate();await rejected;
 assert.throws(()=>process.kill(s.pid,0),/ESRCH/);
});
