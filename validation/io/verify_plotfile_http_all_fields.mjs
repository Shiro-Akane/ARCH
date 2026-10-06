#!/usr/bin/env node
/** Full native field/block HTTP readback. Existing local files only, no simulation. */
import {readFile,writeFile,mkdir,open} from 'node:fs/promises';
import {resolve,relative,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import {performance} from 'node:perf_hooks';
import {openProject} from '../../studio/host/project.ts';
import {createHostServer,listenLocal} from '../../studio/host/server.ts';
import {PROTOCOL_VERSION} from '../../studio/src/host/contracts.ts';
import {validatePlotfileAudit} from '../../studio/src/host/plotfileAudit.ts';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../..');
assert.equal(process.argv.length,4,'Use trusted local launch JSON NEW_LOCAL_OUTPUT');
const source=JSON.parse(await readFile(process.argv[2],'utf8')).oracle;
assert.equal(source.length,2);
const output=resolve(process.argv[3]);await mkdir(output);
const trace=await open(output+'/trace.ndjson','wx');
const project=await openProject({project:root});
const origin='http://127.0.0.1:41999';
const server=createHostServer(project,origin);await listenLocal(server,0);
const endpoint='http://127.0.0.1:'+server.address().port;
const projectId=project.snapshot().session.projectId;
const hash=async path=>createHash('sha256').update(await readFile(path)).digest('hex');
const rows=[];let requests=0,maxBytes=0;
async function request(route,body){
 const response=await fetch(endpoint+'/api/plotfile/audit-'+route,{
  method:'POST',headers:{Origin:origin,'X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION,'Content-Type':'application/json'},
  body:JSON.stringify({projectId,...body}),signal:AbortSignal.timeout(16000)});
 const text=await response.text();requests++;maxBytes=Math.max(maxBytes,Buffer.byteLength(text));
 assert.equal(response.status,200,text);assert(Buffer.byteLength(text)<=128*1024);
 const value=JSON.parse(text);
 await trace.write(JSON.stringify({route,request:body,response:value})+'\n');
 return value;
}
try{
 for(const file of source){
  const path=resolve(file.path),relativePath=relative(root,path);assert(!relativePath.startsWith('..'));
  const sha=await hash(path);assert.equal(sha,file.sha256);
  const started=performance.now();
  const metadata=validatePlotfileAudit(await request('metadata',{relativePath}),projectId,relativePath).audit;
  assert.equal(metadata.file.sha256,sha);
  assert([1,2].includes(metadata.dimension)&&metadata.geometry==='cartesian');
  assert(metadata.cellShape.reduce((a,b)=>a*b,1)<=512);
  let fieldValues=0;
  for(const field of metadata.fields){
   for(let block=0;block<metadata.blocks;block++){
    const slice={field:field.name,block,start:metadata.cellShape.map(()=>0),count:metadata.cellShape};
    const response=await request('slice',{relativePath,slice,expectedFileSha256:sha});
    const checked=validatePlotfileAudit(response,projectId,relativePath,slice,sha);
    assert(checked.audit.payload.nativeCells);
    fieldValues+=checked.audit.payload.values.length;
   }
   console.log(file.producer.case_id+': '+field.name+' all '+metadata.blocks+' blocks read');
  }
  assert.equal(await hash(path),sha);
  rows.push({caseId:file.producer.case_id,fileSha256:sha,fieldCount:metadata.fields.length,
   blocks:metadata.blocks,cells:metadata.cells,fieldValues,wallSeconds:(performance.now()-started)/1000,fileUnchanged:true});
 }
 await writeFile(output+'/transport-summary.json',JSON.stringify({status:'PASS',rows,requests,maxResponseBytes:maxBytes,
  endpoint,scope:'Production HTTP Host and client slice validation; not renderer or scientific oracle'},null,2)+'\n',{flag:'wx'});
}finally{
 await trace.close();await new Promise(r=>server.close(r));
}
