import {spawn} from 'node:child_process';
import {readFileSync,writeFileSync,existsSync} from 'node:fs';
import {resolve,join} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {performance} from 'node:perf_hooks';
import assert from 'node:assert/strict';

// Manual Linux engineering measurement. Caller supplies a frozen scenario;
// this tool does not choose physical controls, thresholds, or long endpoints.
const [rootArg,planArg]=process.argv.slice(2);
assert.equal(process.argv.length,4);
assert.equal(process.platform,'linux');
const root=resolve(rootArg),plan=JSON.parse(readFileSync(planArg,'utf8'));
assert.equal(plan.runs.length,6);
assert(plan.runs.every((r,i)=>r.concurrent===(i%2===1)));
const sha=p=>createHash('sha256').update(readFileSync(p)).digest('hex');
const originalHash=sha(plan.readerFile),binaryHash=sha(plan.binary);
const reader=await import(pathToFileURL(join(root,'studio/host/isolatedPlotfileMetadata.ts')).href);
const readerWorkerPath=join(root,'studio/host/plotfileMetadataWorker.ts');
function workerIds(){
 return readFileSync('/proc/self/task/'+process.pid+'/children','utf8').trim()
 .split(/\s+/).filter(Boolean).filter(pid=>{
  try{return readFileSync('/proc/'+pid+'/cmdline','utf8').split('\0').includes(readerWorkerPath);}
  catch{return false;}
 });
}
function sample(pid){
 try{
  const s=readFileSync('/proc/'+pid+'/status','utf8');
  const io=Object.fromEntries(readFileSync('/proc/'+pid+'/io','utf8').trim().split('\n')
   .map(l=>{const [k,v]=l.split(':');return [k,Number(v)];}));
  return {rssKiB:Number(s.match(/^VmHWM:\s+(\d+)/m)?.[1]??0),
   rchar:io.rchar,readBytes:io.read_bytes};
 }catch{return null;}
}
const delay=ms=>new Promise(r=>setTimeout(r,ms));
const rows=[];
for(const run of plan.runs){
 let stopReader=false,readerFailure,archStarted,archEnded;
 const queries=[],workers=new Map();
 const readerLoop=run.concurrent?(async()=>{
  do{
   const started=performance.now();
   try{
    const response=await reader.readPlotfileOverviewIsolated(plan.readerFile,{field:'DENS',width:32,height:24});
    assert.equal(response.file.sha256,originalHash);
    queries.push({startedMs:started,endedMs:performance.now(),elapsedMs:performance.now()-started,
     responseBytes:Buffer.byteLength(JSON.stringify(response)),scannedCells:response.overview.scannedCells});
   }catch(e){readerFailure=e;break;}
  }while(!stopReader);
 })():Promise.resolve();
 if(run.concurrent){
  const deadline=performance.now()+3000;
  while(workerIds().length===0&&!readerFailure&&performance.now()<deadline)await delay(1);
  assert(workerIds().length>0,'no Reader worker observed before ARCH launch');
 }
 const stdout=[],stderr=[];let archSample=null;
 archStarted=performance.now();
 const child=spawn(plan.binary,['Sod',run.config],
  {cwd:root,env:{...process.env,OMP_NUM_THREADS:'1',CUDA_VISIBLE_DEVICES:''},stdio:['ignore','pipe','pipe'],shell:false});
 child.stdout.on('data',b=>stdout.push(b));child.stderr.on('data',b=>stderr.push(b));
 const timer=setInterval(()=>{
  const m=sample(child.pid);
  if(m)archSample=Object.fromEntries(Object.entries(m).map(([k,v])=>[k,Math.max(v,archSample?.[k]??0)]));
  for(const pid of workerIds()){
   const m=sample(pid),old=workers.get(pid);
   if(m)workers.set(pid,Object.fromEntries(Object.entries(m).map(([k,v])=>[k,Math.max(v,old?.[k]??0)])));
  }
 },5);
 const timeout=setTimeout(()=>child.kill('SIGKILL'),90000);
 let code,signal;
 try{
  [code,signal]=await new Promise((res,rej)=>{child.once('error',rej);child.once('close',(c,s)=>res([c,s]));});
 }finally{
  archEnded=performance.now();clearTimeout(timeout);clearInterval(timer);stopReader=true;await readerLoop;
 }
 writeFileSync(run.stdout,Buffer.concat(stdout));writeFileSync(run.stderr,Buffer.concat(stderr));
 assert.equal(code,0);assert.equal(signal,null);if(readerFailure)throw readerFailure;
 assert.equal(workerIds().length,0);
 assert(!existsSync('/proc/'+child.pid));
 assert.equal(sha(plan.binary),binaryHash);assert.equal(sha(plan.readerFile),originalHash);
 rows.push({concurrent:run.concurrent,config:run.config,elapsedMs:archEnded-archStarted,archSample,
  observedReaderWorkers:[...workers].map(([pid,m])=>({pid:Number(pid),...m})),
  readerQueries:queries.map(q=>({...q,overlapMs:Math.max(0,Math.min(q.endedMs,archEnded)-Math.max(q.startedMs,archStarted))})),
  noOwnedChildAfterSettlement:true});
}
console.log(JSON.stringify({scope:'Small frozen CPU Sod endpoint + existing Cartesian Cellular Plotfile overview',
 node:process.version,binarySha256:binaryHash,readerFileSha256:originalHash,rows,
 limitations:['Warm-cache, three pairs, alternating baseline/concurrent; not a statistical performance acceptance.',
 '5 ms sampling may miss final peaks and exit counters; rchar includes Node/HDF imports.',
 'Existing 5120-cell Reader file only; not large AMR scalability or long O9.',
 'Reader process overhead is part of this measured workflow; ARCH runs with one CPU thread.',
 'Fixed returned pixels do not eliminate whole-file digest and native leaf scanning.']},null,2));
