import {createHash} from 'node:crypto';
/** Linux read-phase cancellation evidence; fixed Host worker, no Core execution. */
import {readFileSync,readlinkSync,readdirSync,statSync,existsSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {performance} from 'node:perf_hooks';
import assert from 'node:assert/strict';
const [projectRoot,input]=process.argv.slice(2);
assert.equal(process.argv.length,4);assert.equal(process.platform,'linux');
const root=resolve(projectRoot),path=resolve(input),bytes=statSync(path).size;
const before=createHash('sha256').update(readFileSync(path)).digest('hex');
const reader=await import(pathToFileURL(join(root,'studio/host/isolatedPlotfileMetadata.ts')).href);
function workers(){
 return readFileSync('/proc/self/task/'+process.pid+'/children','utf8').trim().split(/\s+/).filter(Boolean)
 .filter(pid=>{try{return readFileSync('/proc/'+pid+'/cmdline','utf8').split('\0').includes(join(root,'studio/host/plotfileMetadataWorker.ts'));}catch{return false;}});
}
const controller=new AbortController(),start=performance.now();
let event,settled=false;
const pending=reader.readPlotfileOverviewIsolated(path,{field:'DENS',width:32,height:24},{signal:controller.signal})
 .finally(()=>{settled=true;});
const rejected=assert.rejects(pending,e=>e.code==='CANCELLED');
await assert.rejects(reader.inspectPlotfileMetadataIsolated(path),e=>e.code==='BUSY');
const timer=setInterval(()=>{
 for(const pid of workers())try{
  const io=readFileSync('/proc/'+pid+'/io','utf8');
  const rchar=Number(io.match(/^rchar:\s+(\d+)/m)[1]);
  const openInputDescriptors=readdirSync('/proc/'+pid+'/fd').filter(fd=>{
   try{return readlinkSync('/proc/'+pid+'/fd/'+fd)===path;}catch{return false;}
  }).length;
  if(!settled&&rchar>2*bytes&&openInputDescriptors>=2){
   event={pid:Number(pid),elapsedMs:performance.now()-start,rchar,threshold:2*bytes,openInputDescriptors};
   clearInterval(timer);controller.abort();
  }
 }catch{}
},2);
try{await rejected;}finally{clearInterval(timer);}
const cancelSettledMs=performance.now()-start;
assert(event,'No read-phase event observed; never substitute startup cancellation');
assert.equal(workers().length,0);
assert.equal(existsSync('/proc/'+event.pid),false);
const recoveryStart=performance.now();
const recovered=await reader.readPlotfilePointIsolated(path,{field:'DENS',point:[.5,6.5]});
assert.equal(recovered.payload.values[0],384);
assert.equal(recovered.file.sha256,before);
console.log(JSON.stringify({version:'plotfile-read-phase-cancel-1',event,cancelSettledMs,recoveryMs:performance.now()-recoveryStart,rawFileUnchanged:true,
 cancelledProcessReaped:true,busy:'PASS',recovery:'PASS',fileSha256:recovered.file.sha256,
 limitations:['Read-phase defined by observed open input descriptors and rchar > two file sizes.',
 'rchar includes imports and HDF/WASM calls; this is not an instrumented exact HDF call boundary.',
 'No HTTP/native UI, kernel-uninterruptible I/O, real ENOSPC or concurrent simulation claim.']},null,2));
