import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,readFile,writeFile,rm,readdir} from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {RunPreparationRunner} from '../host/runPreparation.ts';
import {readConfig} from '../host/config.ts';
async function fixture(){
 const root=await mkdtemp(path.join(os.tmpdir(),'run prepare 中文 '));
 const schema=JSON.parse(await readFile(new URL('../../src/api/examples/configuration-v3/schema.json',import.meta.url),'utf8'));
 const inspection=JSON.parse(await readFile(new URL('../../src/api/examples/configuration-v3/sod-valid.json',import.meta.url),'utf8'));
 const registry={schemaVersion:'1.0',version:'1',kind:'registered-cases',status:'ok',setup:'not_executed',cuda:'not_initialized',cases:[
  {caseId:'Sod',initialFieldPreview:true,initialAmrPreview:true,previewDimensions:[1],inspection:{registered:true,command:'--inspect-case',workerPlatform:'linux',setupReads:true,primitiveSinkProbe:true,automaticExpressionInference:false,hostWallTimeoutSeconds:10,coverage:'fixture',reviewedUnitEvidence:'fixture',sourceFile:null,compiledSourceSha256:null}}]};
 const script=`#!${process.execPath}
const crypto=require('node:crypto');
if(process.argv[2]==='--list-cases')process.stdout.write(JSON.stringify(${JSON.stringify(registry)}),()=>process.exit(0));
else if(process.argv[2]==='--config-schema')process.stdout.write(JSON.stringify(${JSON.stringify(schema)}),()=>process.exit(0));
else if(process.argv[2]==='--inspect-config'){
let input='';process.stdin.setEncoding('utf8');process.stdin.on('data',b=>input+=b);process.stdin.on('end',()=>{
const result=${JSON.stringify(inspection)};result.identity={caseId:process.argv[3],requestId:process.argv[6],configRevision:crypto.createHash('sha256').update(input).digest('hex')};
if(input.includes('restart-fixture')){
 result.parameters.find(p=>p.key==='restart').resolvedValue=true;
 const p=result.parameters.find(p=>p.key==='restart_file');p.resolvedValue='restart.h5';p.applicability.state='satisfied';p.requirement.required=true;p.requirement.state='satisfied';p.inputState='present';p.rawValue='restart.h5';p.parsedValue='restart.h5';p.valueSource='input';p.sourceEvidence=null;p.locations=[{source:'stdin',line:1,column:1,endColumn:11,rawValue:'restart.h5'}];
}
if(input.includes('invalid-fixture')){result.status='error';result.completeness.state='invalid';process.exitCode=3;}
setTimeout(()=>console.log(JSON.stringify(result)),input.includes('slow-fixture')?100:0);
});
}else process.exit(99);
`;
 await writeFile(root+'/ARCH',script,{mode:0o755});
 await writeFile(root+'/saved.par','saved fixture\n');
 const config=()=>readConfig(root,'saved.par','project');
 return {root,config,runner:new RunPreparationRunner(root,'project','ARCH',config)};
}
test('preparation binds saved bytes and compiled binary, never claims simulation readiness or creates output',async()=>{
 const f=await fixture();
 try{
  const before=await readdir(f.root),saved=await f.config();
  const result=await f.runner.prepare({projectId:'project',caseId:'Sod',configRevision:saved.fingerprint.sha256,mode:'run'});
  assert.equal(result.canConfirm,true);assert.equal(result.simulationReadiness,'core-startup-pending');
  assert.equal(result.binary.sourceClaim,'compiled-version-only');assert.equal(result.config.fingerprint.sha256,saved.fingerprint.sha256);
  assert.deepEqual(await readdir(f.root),before);
 }finally{await rm(f.root,{recursive:true,force:true});}
});
test('preparation refuses unsaved identity, command authority, unknown case and invalid declared input',async()=>{
 const f=await fixture();
 try{
  const saved=await f.config(),request={projectId:'project',caseId:'Sod',configRevision:saved.fingerprint.sha256,mode:'run' as const};
  await assert.rejects(f.runner.prepare({...request,configRevision:'0'.repeat(64)}),/Save the exact/);
  for(const extra of [{program:'/bin/sh'},{args:[]},{cwd:'/tmp'},{env:{}},{shell:true}])
   await assert.rejects(f.runner.prepare({...request,...extra} as never),/Invalid run preparation/);
  await assert.rejects(f.runner.prepare({...request,caseId:'Unknown'}),/not registered/);
  await writeFile(f.root+'/saved.par','invalid-fixture');
  const invalid=await f.runner.prepare({...request,configRevision:(await f.config()).fingerprint.sha256});
  assert.equal(invalid.canConfirm,false);assert.equal(invalid.inspection.status,'error');
 }finally{await rm(f.root,{recursive:true,force:true});}
});
test('Restart follows saved Core values and readable checkpoint; does not rewrite the config',async()=>{
 const f=await fixture();
 try{
  await writeFile(f.root+'/saved.par','restart-fixture');
  const request={projectId:'project',caseId:'Sod',configRevision:(await f.config()).fingerprint.sha256,mode:'restart' as const};
  assert.equal((await f.runner.prepare(request)).canConfirm,false);
  await writeFile(f.root+'/restart.h5','not a real checkpoint: only metadata preflight');
  const plan=await f.runner.prepare(request);
  assert.equal(plan.canConfirm,true);assert.equal(plan.checkpointPath,f.root+'/restart.h5');
  assert.ok(plan.pendingChecks.some(s=>s.includes('checkpoint identity/layout')));
  assert.equal((await f.runner.prepare({...request,mode:'run'})).canConfirm,false);
  assert.equal(await readFile(f.root+'/saved.par','utf8'),'restart-fixture');
 }finally{await rm(f.root,{recursive:true,force:true});}
});
test('a concurrent saved-file change invalidates a slow preparation',async()=>{
 const f=await fixture();
 try{
  await writeFile(f.root+'/saved.par','slow-fixture');
  const request={projectId:'project',caseId:'Sod',configRevision:(await f.config()).fingerprint.sha256,mode:'run' as const};
  const pending=f.runner.prepare(request);
  const assertion=assert.rejects(pending,/changed during run preparation/);
  await new Promise(resolve=>setTimeout(resolve,80));
  await writeFile(f.root+'/saved.par','changed during preflight');
  await assertion;
 }finally{await rm(f.root,{recursive:true,force:true});}
});

import {createHostServer,listenLocal} from '../host/server.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
test('run preparation HTTP retains Origin, protocol, field authority and method boundaries',async()=>{
 const f=await fixture();
 const server=createHostServer({runPreparation:f.runner,snapshot:()=>({session:{projectId:'project'},host:{}}) as never,refresh:async()=>({}) as never},'http://127.0.0.1:4179');
 try{
  await listenLocal(server,0);
  const url='http://127.0.0.1:'+(server.address() as {port:number}).port+'/api/run/prepare';
  const headers={Origin:'http://127.0.0.1:4179','X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION,'Content-Type':'application/json'};
  const request={projectId:'project',caseId:'Sod',configRevision:(await f.config()).fingerprint.sha256,mode:'run'};
  assert.equal((await fetch(url,{method:'GET',headers})).status,405);
  assert.equal((await fetch(url,{method:'POST',headers:{...headers,Origin:'http://evil'},body:JSON.stringify(request)})).status,403);
  assert.equal((await fetch(url,{method:'POST',headers:{...headers,'X-ARCH-Protocol':'old'},body:JSON.stringify(request)})).status,426);
  assert.equal((await fetch(url,{method:'POST',headers,body:JSON.stringify({...request,args:[]})})).status,400);
  const response=await fetch(url,{method:'POST',headers,body:JSON.stringify(request)});
  assert.equal(response.status,200);
  const plan=await response.json() as {protocolVersion:string;simulationReadiness:string};
  assert.equal(plan.protocolVersion,PROTOCOL_VERSION);assert.equal(plan.simulationReadiness,'core-startup-pending');
 }finally{await new Promise<void>(resolve=>server.close(()=>resolve()));await rm(f.root,{recursive:true,force:true});}
});
import {openProject} from '../host/project.ts';
test('project run preparation reads the current associated saved file without a Preview profile',async()=>{
 const f=await fixture();
 try{
  const project=await openProject({project:f.root,binary:'ARCH',config:'saved.par'});
  assert.equal(project.preview,undefined);assert.ok(project.runPreparation);
  const saved=await project.readConfig();
  const plan=await project.runPreparation.prepare({projectId:saved.projectId,caseId:'Sod',configRevision:saved.fingerprint.sha256,mode:'run'});
  assert.equal(plan.canConfirm,true);assert.equal(plan.config.relativePath,'saved.par');
 }finally{await rm(f.root,{recursive:true,force:true});}
});
