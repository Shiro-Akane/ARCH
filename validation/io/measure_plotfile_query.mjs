/**
 * Read-only Linux measurement of the current audit reader, one query/process.
 * Run with the Studio-declared Node version. Raw files remain local.
 */
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {performance} from 'node:perf_hooks';

const [projectRoot,inputPath,requestJson]=process.argv.slice(2);
if(!projectRoot||!inputPath||process.argv.length>5)
 throw Error('Usage: node measure_plotfile_query.mjs PROJECT_ROOT PLOTFILE [SLICE_OR_OVERVIEW_OR_POINT_JSON]');
if(process.platform!=='linux')throw Error('Measurement requires Linux /proc/self/io.');
const reader=await import(pathToFileURL(resolve(projectRoot,'studio/host/plotfileMetadata.ts')).href);
const request=requestJson?JSON.parse(requestJson):null;
const point=request&&typeof request==='object'&&'point' in request;
const overview=request&&typeof request==='object'&&'width' in request;
function ioCounters(){
 const result={};
 for(const line of readFileSync('/proc/self/io','utf8').trim().split('\n')){
  const [key,value]=line.split(':');result[key]=BigInt(value.trim());
 }
 return result;
}
const before=ioCounters(),cpu=process.cpuUsage(),start=performance.now();
const response=point?await reader.readPlotfilePoint(resolve(inputPath),request):overview?await reader.readPlotfileOverview(resolve(inputPath),request):request
 ?await reader.readPlotfileFieldSlice(resolve(inputPath),request)
 :await reader.inspectPlotfileMetadata(resolve(inputPath));
const elapsedMs=performance.now()-start,cpuDelta=process.cpuUsage(cpu),after=ioCounters();
const responseBytes=Buffer.byteLength(JSON.stringify(response),'utf8');
const usage=process.resourceUsage();
const difference=key=>(after[key]-before[key]).toString();
console.log(JSON.stringify({
 measurementVersion:'plotfile-query-cost-1',
 mode:point?'point':overview?'overview':request?'slice':'metadata',request,
 runtime:{node:process.version,platform:process.platform,arch:process.arch},
 file:{sha256:response.file.sha256,bytes:response.file.bytes},
 structure:{blocks:response.blocks,cells:response.cells,cellShape:response.cellShape},
 responseBytes,elapsedMs,cpuMicroseconds:cpuDelta,
 processPeakRssKiB:usage.maxRSS,
 io:{
  processLogicalReadBytes:difference('rchar'),
  processStorageReadBytes:difference('read_bytes'),
  processReadSyscalls:difference('syscr'),
  readerDigestBytes:response.file.bytes,
  hdf5DatasetReadBytes:null,
 },
 limitations:[
  'Peak RSS includes Node, imports and HDF5 WASM; it is not incremental payload memory.',
  'Linux rchar covers process read calls during the query, including non-HDF5 reads.',
  'Linux read_bytes is storage I/O charged to this process; cached reads may report zero.',
  'The current reader streams a whole-file digest on every query.',
  'No per-dataset HDF5 byte counter is instrumented; do not infer it from response size.',
  'This tool neither drops caches nor establishes a cold-cache benchmark.',
  'Audit budgets remain unchanged; small local files do not establish large-file LOD scalability.',
 ],
},null,2));
