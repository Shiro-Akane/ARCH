import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import {previewFixture} from './preview-fixture.ts';
import {makeManifest,inputs,saveManifest} from '../host/buildManifest.ts';
import {profilesFromModels} from '../host/previewProfile.ts';
import {validateModelCapabilities} from '../src/host/previewValidation.ts';

test('runtime model dimensions create separate bounded Host-owned profiles',()=>{
 const models=validateModelCapabilities([{caseId:'Gaussian',dimensions:[1,2,3],
  geometries:['cartesian','spherical','cylindrical'],previewBackend:'cpu',
  fields:['DENS','PRES','VELZ'],maxFields:8,maxResponseBytes:8388608,
  samplingByDimension:[
   {dimension:1,defaultShape:[512],minPerAxis:2,maxPerAxis:4096,maxTotalSamples:4096},
   {dimension:2,defaultShape:[128,128],minPerAxis:2,maxPerAxis:256,maxTotalSamples:65536},
   {dimension:3,defaultShape:[32,32,32],minPerAxis:2,maxPerAxis:64,maxTotalSamples:32768}]}]);
 const profiles=profilesFromModels(models,'managed-build');
 assert.deepEqual(profiles.map(p=>p.dimension),[1,2,3]);
 assert.equal(new Set(profiles.map(p=>p.id)).size,3);
 assert.ok(profiles.every(p=>p.caseId==='Gaussian'&&p.buildProfileId==='managed-build'));
 assert.deepEqual(profiles[2].defaultShape,[32,32,32]);
 assert.equal(profiles[2].maxSampleCount,32768);
 const invalid=structuredClone(models);
 invalid[0].samplingByDimension![2].defaultShape=[64,64,64];
 assert.throws(()=>validateModelCapabilities(invalid),/model sampling/);
});

test('Host shutdown reaps an outstanding capability query and forbids restart',async()=>{
 const f=await previewFixture();
 try{
  const pidFile=f.root+'/capability.pid';
  const script='#!'+process.execPath+'\n'
   +'require("node:fs").writeFileSync('+JSON.stringify(pidFile)+',String(process.pid));'
   +'process.on("SIGTERM",()=>{});setInterval(()=>{},100);\n';
  await writeFile(f.root+'/build/bin/ARCH',script,{mode:0o755});
  const manifest=await makeManifest(f.p,'p','query-build',new Date().toISOString(),await inputs(f.p),undefined,{});
  await saveManifest(f.p,manifest);await f.build.initialize();
  const readiness=f.preview.readiness();
  let pid=0;const until=Date.now()+2000;
  while(!pid&&Date.now()<until){
   try{pid=Number(await readFile(pidFile,'utf8'));}catch{/* wait for actual child */}
   if(!pid)await new Promise(r=>setTimeout(r,10));
  }
  assert.ok(pid>0,'actual capability child must be running');
  process.kill(pid,0);
  const started=Date.now();await f.preview.shutdown();
  assert.ok(Date.now()-started<1000,'shutdown must promptly reap the query');
  assert.throws(()=>process.kill(pid,0),(e:unknown)=>(e as NodeJS.ErrnoException).code==='ESRCH');
  assert.equal((await readiness).ready,false);
  await assert.rejects(f.preview.start(f.request),/Host is closed/);
 }finally{await f.preview.shutdown();await f.cleanup();}
});
