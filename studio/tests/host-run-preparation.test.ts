import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,readFile,writeFile,rm,readdir,mkdir} from 'node:fs/promises';
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
}else if(process.argv[2]==='Sod')setTimeout(()=>process.exit(0),20);else process.exit(99);
`;
 await mkdir(root+'/studio');
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
  assert.ok(project.configuration);
  const schema=await project.configuration.schema();
  assert.match(schema.buildId,/^selected-binary:[a-f0-9]{64}$/);
  const bytes=await project.readConfig();
  const inspected=await project.configuration.inspect({projectId:bytes.projectId,caseId:'Sod',configText:bytes.text,configRevision:bytes.fingerprint.sha256});
  assert.equal(inspected.identity.binarySha256,schema.binarySha256);
  assert.equal(inspected.core.status,'ok');
  await assert.rejects(project.configuration.inspect({projectId:bytes.projectId,caseId:'NotRegistered',configText:bytes.text,configRevision:bytes.fingerprint.sha256}),/not registered/);

  const saved=await project.readConfig();
  const plan=await project.runPreparation.prepare({projectId:saved.projectId,caseId:'Sod',configRevision:saved.fingerprint.sha256,mode:'run'});
  assert.equal(plan.canConfirm,true);assert.equal(plan.config.relativePath,'saved.par');
 }finally{await rm(f.root,{recursive:true,force:true});}
});

import {RunController} from '../host/runController.ts';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const worker=fileURLToPath(new URL('../host/runWorker.ts',import.meta.url));
test('confirmation consumes a Host-held plan once and ignores mutations to the returned object',async()=>{
 const f=await fixture();
 try{
  const request={projectId:'project',caseId:'Sod',configRevision:(await f.config()).fingerprint.sha256,mode:'run' as const};
  const plan=await f.runner.prepare(request),sha=plan.binary.fingerprint.sha256;
  plan.binary.fingerprint.sha256='0'.repeat(64);
  const confirmation={projectId:'project',planId:plan.planId,confirmation:'run-saved-input-with-compiled-binary' as const};
  await assert.rejects(f.runner.consume({...confirmation,program:'/bin/sh'} as never),/confirmation required/);
  const consumed=await f.runner.consume(confirmation);
  assert.equal(consumed.plan.binary.fingerprint.sha256,sha);
  await assert.rejects(f.runner.consume(confirmation),/unavailable/);
 }finally{await rm(f.root,{recursive:true,force:true});}
});
test('changed confirmed input prevents terminal launch; plan cannot be replayed',async()=>{
 const f=await fixture();let launched=false;
 try{
  const plan=await f.runner.prepare({projectId:'project',caseId:'Sod',configRevision:(await f.config()).fingerprint.sha256,mode:'run'});
  const run=new RunController(f.runner,{terminal:async()=>{launched=true;throw new Error('must not launch');}});
  await writeFile(f.root+'/saved.par','changed after prepare');
  const confirmation={projectId:'project',planId:plan.planId,confirmation:'run-saved-input-with-compiled-binary' as const};
  await assert.rejects(run.start(confirmation),/changed after preparation/);
  assert.equal(launched,false);await assert.rejects(run.start(confirmation),/unavailable/);
 }finally{await rm(f.root,{recursive:true,force:true});}
});
test('confirmed handoff stores exact bytes, retains status and completes independently',async()=>{
 const f=await fixture();
 try{
  const plan=await f.runner.prepare({projectId:'project',caseId:'Sod',configRevision:(await f.config()).fingerprint.sha256,mode:'run'});
  const run=new RunController(f.runner,{terminal:async directory=>{
   const child=spawn(process.execPath,[worker,directory],{detached:true,stdio:'ignore'});
   await new Promise<void>((resolve,reject)=>{child.once('spawn',resolve);child.once('error',reject);});child.unref();
   return {pid:child.pid!,exited:()=>child.exitCode!==null||child.signalCode!==null};
  }});
  const launched=await run.start({projectId:'project',planId:plan.planId,confirmation:'run-saved-input-with-compiled-binary'});
  assert.equal(await readFile(f.root+'/studio/.local/runs/'+launched.runId+'/input.par','utf8'),await readFile(f.root+'/saved.par','utf8'));
  let state=await run.status(launched.runId);
  for(let n=0;n<50&&!state.finishedAt;n++){await new Promise(r=>setTimeout(r,20));state=await run.status(launched.runId);}
  assert.equal(state.state,'succeeded');assert.equal(state.exitCode,0);
  await assert.rejects(run.stop(launched.runId),/already finished/);
  const reopened=new RunController(f.runner);
  const records=await reopened.history();
  assert.equal(records.length,1);assert.equal(records[0].runId,launched.runId);
  assert.equal(records[0].state?.state,'succeeded');assert.equal(records[0].configPath,'saved.par');
  await writeFile(f.root+'/studio/.local/runs/'+launched.runId+'/state.json','corrupt');
  const unavailable=await reopened.history();
  assert.equal(unavailable[0].state,null);assert.ok(unavailable[0].diagnostic);
  assert.equal(unavailable[0].configSha,plan.config.fingerprint.sha256);
 }finally{await rm(f.root,{recursive:true,force:true});}
});

test('run start/status/Stop HTTP rejects command authority and uses only the confirmed plan',async()=>{
 const f=await fixture();
 const runs=new RunController(f.runner,{terminal:async directory=>{
  const child=spawn(process.execPath,[worker,directory],{detached:true,stdio:'ignore'});
  await new Promise<void>((resolve,reject)=>{child.once('spawn',resolve);child.once('error',reject);});child.unref();
  return {pid:child.pid!,exited:()=>child.exitCode!==null||child.signalCode!==null};
 }});
 const server=createHostServer({runPreparation:f.runner,runs,snapshot:()=>({session:{projectId:'project'},host:{}}) as never,refresh:async()=>({}) as never},'http://127.0.0.1:4179');
 try{
  await listenLocal(server,0);
  const base='http://127.0.0.1:'+(server.address() as {port:number}).port;
  const headers={Origin:'http://127.0.0.1:4179','X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION,'Content-Type':'application/json'};
  const plan=await f.runner.prepare({projectId:'project',caseId:'Sod',configRevision:(await f.config()).fingerprint.sha256,mode:'run'});
  const confirmation={projectId:'project',planId:plan.planId,confirmation:'run-saved-input-with-compiled-binary'};
  assert.equal((await fetch(base+'/api/run',{method:'POST',headers,body:JSON.stringify({...confirmation,program:'/bin/sh'})})).status,400);
  const started=await fetch(base+'/api/run',{method:'POST',headers,body:JSON.stringify(confirmation)});
  assert.equal(started.status,202);
  const result=await started.json() as {runId:string};
  assert.equal((await fetch(base+'/api/run/'+result.runId+'/stop',{method:'POST',headers,body:'{"pid":1}'})).status,400);
  const status=await fetch(base+'/api/run/'+result.runId,{headers});assert.equal(status.status,200);
  assert.equal((await fetch(base+'/api/run',{method:'POST',headers,body:JSON.stringify(confirmation)})).status,409);
  assert.equal((await fetch(base+'/api/run/aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa',{headers})).status,404);
  for(let n=0;n<50&&!(await runs.status(result.runId)).finishedAt;n++)await new Promise(r=>setTimeout(r,20));
  assert.equal((await runs.status(result.runId)).state,'succeeded');
 }finally{await new Promise<void>(resolve=>server.close(()=>resolve()));await rm(f.root,{recursive:true,force:true});}
});
test('checkpoint disappearance after preparation is rejected before confirmation is consumed into a job',async()=>{
 const f=await fixture();
 try{
  await writeFile(f.root+'/saved.par','restart-fixture');await writeFile(f.root+'/restart.h5','metadata-only fixture');
  const plan=await f.runner.prepare({projectId:'project',caseId:'Sod',configRevision:(await f.config()).fingerprint.sha256,mode:'restart'});
  await rm(f.root+'/restart.h5');
  await assert.rejects(f.runner.consume({projectId:'project',planId:plan.planId,confirmation:'run-saved-input-with-compiled-binary'}),/Resource preflight changed/);
 }finally{await rm(f.root,{recursive:true,force:true});}
});
