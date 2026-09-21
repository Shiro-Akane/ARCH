import test from 'node:test';
import assert from 'node:assert/strict';
import {ConfigurationAdapter} from '../host/configuration.ts';
import {previewFixture} from './preview-fixture.ts';
import {createHostServer,listenLocal} from '../host/server.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
test('configuration rejects browser command authority and stale config identity before spawning',async()=>{
 const f=await previewFixture();try{
  const adapter=new ConfigurationAdapter(f.preview);
  const request={projectId:'p',caseId:'Sod' as const,configText:f.request.configText,configRevision:f.request.configRevision};
  for(const extra of [{program:'/bin/sh'},{argv:[]},{args:[]},{cwd:'/tmp'},{env:{}},{shell:true},{caseId:'Other'},{projectId:'other'},{configRevision:'old'},{configText:'x'.repeat(1048577)},{configText:'\0'}])await assert.rejects(adapter.inspect({...request,...extra} as never),/Invalid configuration/);
 }finally{await f.cleanup();}
});
test('configuration endpoints retain exact origin, protocol and request field boundaries',async()=>{
 const f=await previewFixture();const reader={configuration:new ConfigurationAdapter(f.preview),snapshot:()=>({session:{projectId:'p'},host:{}}) as never,refresh:async()=>({}) as never};
 const server=createHostServer(reader,'http://127.0.0.1:4179');try{
  await listenLocal(server,0);const url=`http://127.0.0.1:${(server.address() as {port:number}).port}/api/configuration/inspect`;
  const headers={Origin:'http://127.0.0.1:4179','X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION,'Content-Type':'application/json'};
  assert.equal((await fetch(url,{method:'POST',headers:{...headers,Origin:'http://evil'},body:'{}'})).status,403);
  assert.equal((await fetch(url,{method:'POST',headers:{...headers,'X-ARCH-Protocol':'1.2'},body:'{}'})).status,426);
  assert.equal((await fetch(url,{method:'POST',headers,body:JSON.stringify({projectId:'p',caseId:'Sod',configText:f.request.configText,configRevision:f.request.configRevision,shell:true})})).status,400);
 }finally{await new Promise<void>(resolve=>server.close(()=>resolve()));await f.cleanup();}
});

import {readFile,writeFile} from 'node:fs/promises';
import {inputs,makeManifest,saveManifest} from '../host/buildManifest.ts';
import {createHash} from 'node:crypto';
test('fixed inspection process returns exact stdin identity, retains structured errors and rejects changed build',async()=>{
 const f=await previewFixture();try{
  const payload=JSON.parse(await readFile(new URL('../../src/api/examples/configuration/inspect-sod.json',import.meta.url),'utf8'));
  const script=`#!${process.execPath}
const crypto=require('node:crypto');let input='';process.stdin.setEncoding('utf8');process.stdin.on('data',b=>input+=b);process.stdin.on('end',()=>{if(process.argv[2]!=='--inspect-config'||process.argv[4]!=='--config-stdin'||process.argv[5]!=='--request-id')process.exit(2);const result=${JSON.stringify(payload)};result.identity={caseId:process.argv[3],requestId:process.argv[6],configRevision:crypto.createHash('sha256').update(input).digest('hex')};if(input.includes('invalid')){result.status='error';result.diagnostics=[{severity:'error',code:'INVALID_INTEGER',parameterKey:'nblockx1',message:'invalid'}];process.exitCode=3;}setTimeout(()=>console.log(JSON.stringify(result)),input.includes('slow')?100:0);});`;
  await writeFile(f.root+'/build/bin/ARCH',script,{mode:0o755});await writeFile(f.root+'/disk.par','original');
  await saveManifest(f.p,await makeManifest(f.p,'p','configuration-build',new Date().toISOString(),await inputs(f.p),undefined,{}));await f.build.initialize();
  const adapter=new ConfigurationAdapter(f.preview);
  const request=(configText:string)=>({projectId:'p',caseId:'Sod' as const,configText,configRevision:createHash('sha256').update(configText).digest('hex')});
  const result=await adapter.inspect(request('nblockx1=8\n# unsaved\n'));
  assert.equal(result.identity.configRevision,request('nblockx1=8\n# unsaved\n').configRevision);assert.equal(result.identity.buildId,'configuration-build');assert.equal(await readFile(f.root+'/disk.par','utf8'),'original');
  assert.equal((await adapter.inspect(request('invalid'))).core.status,'error');
  const pending=adapter.inspect(request('slow'));await new Promise(resolve=>setTimeout(resolve,40));await writeFile(f.root+'/case.cpp','changed');await assert.rejects(pending,/current successful Preview build|Build changed/);
 }finally{await f.cleanup();}
});
