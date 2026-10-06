/** Compare complete responses outside raw-data submissions; fixed read-only queries. */
import {readFileSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
const [projectRoot,runsPath]=process.argv.slice(2);
assert.equal(process.argv.length,4);
const root=resolve(projectRoot);
const reader=await import(pathToFileURL(join(root,'studio/host/isolatedPlotfileMetadata.ts')).href);
const {stringifyPlotfile}=await import(pathToFileURL(join(root,'studio/host/plotfileJson.ts')).href);
const runs=JSON.parse(readFileSync(runsPath,'utf8'));
assert(Array.isArray(runs)&&runs.length>0&&runs.length<=4);
const rows=[];
for(const run of runs){
 const path=join(run.localEvidenceDirectory,'output/reference_HLLC_plt_0000.h5');
 const rawHash=()=>createHash('sha256').update(readFileSync(path)).digest('hex');
 const before=rawHash(),m=await reader.inspectPlotfileMetadataIsolated(path);
 const overview=await reader.readPlotfileOverviewIsolated(path,{field:'DENS',width:32,height:m.dimension===1?1:24});
 const point=await reader.readPlotfilePointIsolated(path,{field:'DENS',point:m.dimension===1?[.49]:[.5,6.5]});
 const slice=await reader.readPlotfileFieldSliceIsolated(path,{field:'DENS',block:0,start:m.cellShape.map(()=>0),count:m.cellShape.map((n,i)=>i===m.cellShape.length-1?Math.min(n,8):1)});
 const responses=Object.fromEntries(Object.entries({metadata:m,overview,point,slice}).map(([key,value])=>[key,{
  sha256:createHash('sha256').update(stringifyPlotfile(value)).digest('hex'),
  bytes:Buffer.byteLength(stringifyPlotfile(value))
 }]));
 assert.equal(rawHash(),before);
 assert.equal(point.file.sha256,before);
 rows.push({case:run.case,fileSha256:before,responses,rawFileUnchanged:true});
}
console.log(JSON.stringify({version:'plotfile-query-response-digests-1',rows,
 scope:'Complete numeric JSON response digests; raw HDF and arrays stay local; no science certification'},null,2));
