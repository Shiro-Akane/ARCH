import test from 'node:test';
import assert from 'node:assert/strict';
import {BuildProjectRefresh} from '../src/host/BuildProjectRefresh.ts';
import {validateSnapshot} from '../src/host/LocalHostAdapter.ts';
import type {BuildSnapshot} from '../src/host/contracts.ts';
const now='2026-10-04T00:00:00.000Z',old='a'.repeat(64),fresh='b'.repeat(64);
function project(sha=old,id='p'){
 return validateSnapshot({host:{protocolVersion:'1.3',hostKind:'local',platform:'linux',projectRoot:'/project',capabilities:{readProject:true,writeConfig:true,build:true,preview:true,watchFiles:false}},
 session:{projectId:id,displayName:'Project',projectRoot:'/project',openedAt:now,refreshedAt:now,mapping:'unknown',metadata:'unavailable',sourceState:'unknown',configFileState:'available',binaryState:'available',
 executable:{relativePath:'build/bin/ARCH',kind:'executable',exists:true,changed:sha!==old,sha256:sha,size:12,modifiedTime:now}}});
}
function build():BuildSnapshot{
 return {protocolVersion:'1.3',projectId:'p',configured:true,state:'succeeded',mappingState:'configured',binaryState:'freshness-unknown',freshnessReason:'incomplete',changedInputs:[],
 latestResult:{projectId:'p',buildId:'b',state:'succeeded',startedAt:now,finishedAt:now},
 lastSuccessfulBuild:{manifestVersion:'1',buildId:'b',projectId:'p',profileId:'cpu',managedSourceRoot:'/project',buildProfileFingerprint:old,trackedInputFingerprints:[],preBuildInputFingerprints:[],inputsStableDuringBuild:true,buildDirectory:'/project/build',target:'ARCH',startedAt:now,finishedAt:now,outputBinary:{relativePath:'build/bin/ARCH',absolutePath:'/project/build/bin/ARCH',fingerprint:{sha256:fresh,size:12,modifiedTime:now}}}};
}
test('successful Build reconciles selected binary once, retaining config and freshness semantics',async()=>{
 const sync=new BuildProjectRefresh(),before=project();let calls=0;const accepted:unknown[]=[];
 await sync.observe(build(),before,async()=>{calls++;return project(fresh);},()=>true,p=>accepted.push(p));
 await sync.observe(build(),before,async()=>{calls++;return project(fresh);},()=>true,p=>accepted.push(p));
 assert.equal(calls,1);assert.deepEqual(accepted,[project(fresh)]);
 assert.equal(build().binaryState,'freshness-unknown');
});
test('active, failed, other project/source/output and already-matching binary never refresh',async()=>{
 for(const kind of ['active','failed','project','source','output','matching']){
  const b=build();let p=project();if(kind==='active')b.activeBuildId='active';
  if(kind==='failed'){b.state='failed';b.latestResult!.state='failed';}
  if(kind==='project')p=project(old,'other');
  if(kind==='source')b.lastSuccessfulBuild!.managedSourceRoot='/other';
  if(kind==='output')b.lastSuccessfulBuild!.outputBinary.relativePath='other/ARCH';
  if(kind==='matching')p=project(fresh);
  await new BuildProjectRefresh().observe(b,p,async()=>{throw Error('unexpected refresh '+kind);},()=>true,()=>assert.fail());
 }
});
test('late project refresh cannot overwrite a switched project or newer snapshot',async()=>{
 let current=true;let resolve!:(p:ReturnType<typeof project>)=>void;
 const pending=new Promise<ReturnType<typeof project>>(r=>{resolve=r;});
 const operation=new BuildProjectRefresh().observe(build(),project(),()=>pending,()=>current,()=>assert.fail('stale update'));
 current=false;resolve(project(fresh));await operation;
});
test('refresh failure and mismatching binary remain explicit; polling does not loop requests',async()=>{
 const sync=new BuildProjectRefresh();let calls=0;
 const refresh=async()=>{calls++;throw Error('offline');};
 await assert.rejects(sync.observe(build(),project(),refresh,()=>true,()=>assert.fail()),/offline/);
 await assert.rejects(sync.observe(build(),project(),refresh,()=>true,()=>assert.fail()),/offline/);assert.equal(calls,1);
 await assert.rejects(new BuildProjectRefresh().observe(build(),project(),async()=>project('c'.repeat(64)),()=>true,()=>assert.fail()),/does not match/);
});
