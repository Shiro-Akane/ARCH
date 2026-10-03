import {readFileSync,existsSync} from 'node:fs';
import {resolve,join} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {performance} from 'node:perf_hooks';
import assert from 'node:assert/strict';
// Manual read-only Linux production-worker probe. Emits no full field arrays.
const [projectRoot,runsPath]=process.argv.slice(2);
if(!projectRoot||!runsPath||process.argv.length!==4||process.platform!=='linux')
 throw Error('Usage: node measure_plotfile_isolation.mjs PROJECT_ROOT RUNS_JSON');
const root=resolve(projectRoot);
const reader=await import(pathToFileURL(join(root,'studio/host/isolatedPlotfileMetadata.ts')).href);
const runs=JSON.parse(readFileSync(runsPath,'utf8'));
assert(Array.isArray(runs)&&runs.length>0&&runs.length<=2);
function workers(){
 return readFileSync('/proc/self/task/'+process.pid+'/children','utf8').trim().split(/\s+/).filter(Boolean)
 .filter(pid=>{try{return readFileSync('/proc/'+pid+'/cmdline','utf8').split('\0').includes(join(root,'studio/host/plotfileMetadataWorker.ts'));}catch{return false;}});
}
async function measure(label,call){
 const metrics=new Map(),start=performance.now();
 const timer=setInterval(()=>{
  for(const pid of workers())try{
   const status=readFileSync('/proc/'+pid+'/status','utf8');
   const io=Object.fromEntries(readFileSync('/proc/'+pid+'/io','utf8').trim().split('\n').map(s=>{const [k,v]=s.split(':');return [k,Number(v)];}));
   const next={rssKiB:Number(status.match(/^VmHWM:\s+(\d+)/m)?.[1]??0),rchar:io.rchar,readBytes:io.read_bytes};
   const old=metrics.get(pid)??{};
   metrics.set(pid,Object.fromEntries(Object.entries(next).map(([k,v])=>[k,Math.max(v,old[k]??0)])));
  }catch{}
 },5);
 let response,error;
 try{response=await call();}catch(e){error={code:e.code??null,message:e.message};}
 finally{clearInterval(timer);}
 assert.equal(workers().length,0,'worker remained after settlement');
 return {response,summary:{label,elapsedMs:performance.now()-start,error,
  responseBytes:response?Buffer.byteLength(JSON.stringify(response)):0,
  fileSha256:response?.file.sha256??null,scannedCells:response?.overview?.scannedCells??null,
  observedWorkers:[...metrics].map(([pid,v])=>({pid:Number(pid),...v}))}};
}
const rows=[];
for(const run of runs){
 const path=join(run.localEvidenceDirectory,'output/reference_HLLC_plt_0000.h5');
 const hash=()=>createHash('sha256').update(readFileSync(path)).digest('hex');
 const before=hash(),queries=[];
 const initial=await measure('metadata',()=>reader.inspectPlotfileMetadataIsolated(path));
 assert.equal(initial.summary.error,undefined);queries.push(initial.summary);
 const m=initial.response,request={field:'DENS',width:32,height:m.dimension===1?1:24};
 for(let n=0;n<2;n++){
  const q=await measure('overview-'+n,()=>reader.readPlotfileOverviewIsolated(path,request));
  assert.equal(q.summary.error,undefined);queries.push(q.summary);
 }
 const q=await measure('point',()=>reader.readPlotfilePointIsolated(path,{field:'DENS',point:m.dimension===1?[.49]:[.5,6.5]}));
 assert.equal(q.summary.error,undefined);queries.push(q.summary);
 const rawPoint={value:q.response.payload.values[0],index:q.response.payload.linearIndices[0]};
 const controller=new AbortController();let observed=[];
 const cancellation=measure('cancel-after-worker-observed',()=>{
  const pending=reader.readPlotfileOverviewIsolated(path,request,{signal:controller.signal});
  const timer=setInterval(()=>{const ids=workers();if(ids.length){observed=ids;clearInterval(timer);controller.abort();}},1);
  return pending.finally(()=>clearInterval(timer));
 });
 await assert.rejects(reader.inspectPlotfileMetadataIsolated(path),e=>e.code==='BUSY');
 const cancelled=await cancellation;assert.equal(cancelled.summary.error?.code,'CANCELLED');
 assert(observed.length>0&&observed.every(pid=>!existsSync('/proc/'+pid)));queries.push(cancelled.summary);
 const recovered=await measure('metadata-after-cancel',()=>reader.inspectPlotfileMetadataIsolated(path));
 assert.equal(recovered.summary.error,undefined);queries.push(recovered.summary);
 assert.equal(hash(),before);assert(queries.filter(q=>!q.error).every(q=>q.fileSha256===before));
 rows.push({case:run.case,bytes:m.file.bytes,fields:m.fields.length,cells:m.cells,fileSha256:before,
  rawPoint,queries,cancelBusyRecovery:'PASS',rawFileUnchanged:true});
}
console.log(JSON.stringify({version:'isolated-plotfile-cost-1',node:process.version,rows,
 limitations:['5ms observed worker HWM/counters may miss final peak and exit counters.',
 'Worker counters include Node/import/WASM; rchar is not dataset-only reads.',
 'Hash checks warm OS cache; not a cold-cache benchmark.',
 'Each query starts a fresh worker; no cross-query cache; full digest and leaf scan remain.',
 'No HTTP/native UAT, large-file scalability or simultaneous simulation claim.']},null,2));
