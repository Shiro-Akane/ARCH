import assert from 'node:assert/strict';
import test from 'node:test';
import {validateBuildSnapshot} from '../src/host/BuildAdapter.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
import {LOCAL_CPU_PROFILE_ID,localCpuProfile} from '../host/localBuildProfile.ts';

const PROJECT_ID='w2-observed-project';
const BUILD_ID='01234567-89ab-4def-8abc-0123456789ab';
const SOURCE_ROOT='C:/work/ARCH';
const OBSERVED_SCOPE='observed-build-inputs-v1';
const FINGERPRINT='a'.repeat(64);

function profile(){
 return {
  id:LOCAL_CPU_PROFILE_ID,
  displayName:'Local CPU Release',
  managedSourceRoot:SOURCE_ROOT,
  buildDirRelative:'build-studio-cpu',
  outputBinaryRelative:'build-studio-cpu/bin/ARCH',
  target:'ARCH',
  parallelism:4,
  compilerDependencyMode:'ninja',
  retainGnuLtoInputs:true,
  linkDependencyFile:'ARCH.link.d',
  dependenciesComplete:false,
  buildBackend:'cpu',
  observedInputScope:OBSERVED_SCOPE,
  registeredCases:['Sod','CellularDet'],
  trackedInputs:['CMakeLists.txt','CMakePresets.json','build-studio-cpu/CMakeCache.txt']
 };
}

function manifest(){
 return {
  manifestVersion:'1',
  buildId:BUILD_ID,
  projectId:PROJECT_ID,
  profileId:LOCAL_CPU_PROFILE_ID,
  managedSourceRoot:SOURCE_ROOT,
  buildProfileFingerprint:FINGERPRINT,
  trackedInputFingerprints:[{relativePath:'CMakeLists.txt',fingerprint:{sha256:FINGERPRINT,size:2048,modifiedTime:'2026-10-25T08:00:00.000Z'}}],
  preBuildInputFingerprints:[],
  inputsStableDuringBuild:true,
  buildDirectory:'build-studio-cpu',
  target:'ARCH',
  startedAt:'2026-10-25T08:00:00.000Z',
  finishedAt:'2026-10-25T08:01:00.000Z',
  outputBinary:{
   relativePath:'build-studio-cpu/bin/ARCH',
   absolutePath:SOURCE_ROOT+'/build-studio-cpu/bin/ARCH',
   fingerprint:{sha256:FINGERPRINT,size:4096,modifiedTime:'2026-10-25T08:01:00.000Z'}
  }
 };
}

function validSnapshot(){
 return {
  protocolVersion:PROTOCOL_VERSION,
  projectId:PROJECT_ID,
  profile:profile(),
  configured:true,
  state:'succeeded',
  mappingState:'configured',
  binaryState:'freshness-unknown',
  freshnessReason:'Binary fingerprint matches the recorded tracked inputs.',
  changedInputs:[],
  observedInputs:{scope:OBSERVED_SCOPE,state:'current',reason:'Tracked inputs match the last successful build.'},
  lastSuccessfulBuild:manifest(),
  latestResult:{
   buildId:BUILD_ID,
   projectId:PROJECT_ID,
   state:'succeeded',
   exitCode:0,
   signal:null,
   startedAt:'2026-10-25T08:00:00.000Z',
   finishedAt:'2026-10-25T08:01:00.000Z'
  }
 };
}

function legacySnapshot(){
 const snapshot=validSnapshot();
 delete snapshot.observedInputs;
 delete snapshot.profile.observedInputScope;
 delete snapshot.profile.buildBackend;
 return snapshot;
}

function assertRejected(label:string,mutate:(snapshot:ReturnType<typeof validSnapshot>)=>void){
 const snapshot=validSnapshot();
 mutate(snapshot);
 assert.throws(()=>validateBuildSnapshot(snapshot,PROJECT_ID),Error,label);
}

test('accepts legacy and every optional observed state with a bounded reason',()=>{
 const legacy=legacySnapshot();
 assert.equal(validateBuildSnapshot(legacy,PROJECT_ID),legacy);

 const current=validSnapshot();
 assert.equal(current.binaryState,'freshness-unknown');
 assert.equal(current.observedInputs.state,'current');
 assert.equal(validateBuildSnapshot(current,PROJECT_ID),current);

 const changed=validSnapshot();
 changed.observedInputs={scope:OBSERVED_SCOPE,state:'changed',reason:'A tracked input changed.'};
 changed.binaryState='needs-build';
 changed.changedInputs=['CMakeLists.txt'];
 assert.equal(validateBuildSnapshot(changed,PROJECT_ID),changed);

 const unknown=validSnapshot();
 unknown.observedInputs={scope:OBSERVED_SCOPE,state:'unknown',reason:'x'.repeat(4096)};
 unknown.mappingState='unknown';
 unknown.binaryState='freshness-unknown';
 assert.equal(validateBuildSnapshot(unknown,PROJECT_ID),unknown);
});

test('rejects malformed observed facets at their exact boundaries',()=>{
 const cases:[string,(snapshot:ReturnType<typeof validSnapshot>)=>void][]=[
  ['unknown scope',snapshot=>{snapshot.observedInputs.scope='observed-build-inputs-v2';}],
  ['coercible object state',snapshot=>{snapshot.observedInputs.state={toString:()=> 'current'};}],
  ['array state',snapshot=>{snapshot.observedInputs.state=['current'];}],
  ['null state',snapshot=>{snapshot.observedInputs.state=null;}],
  ['invalid state',snapshot=>{snapshot.observedInputs.state='pending';}],
  ['coercible object reason',snapshot=>{snapshot.observedInputs.reason={toString:()=> 'known'};}],
  ['empty reason',snapshot=>{snapshot.observedInputs.reason='';}],
  ['whitespace-only reason',snapshot=>{snapshot.observedInputs.reason=' \t\n ';}],
  ['NUL reason',snapshot=>{snapshot.observedInputs.reason='known\0not-known';}],
  ['reason over 4096 characters',snapshot=>{snapshot.observedInputs.reason='x'.repeat(4097);}],
  ['extra facet field',snapshot=>{snapshot.observedInputs.evidence='trusted';}],
  ['array facet',snapshot=>{snapshot.observedInputs=[];}],
  ['null facet',snapshot=>{snapshot.observedInputs=null;}]
 ];
 for(const [label,mutate] of cases)assertRejected(label,mutate);
});

test('rejects unknown, mismatched or incomplete observed profiles',()=>{
 const cases:[string,(snapshot:ReturnType<typeof validSnapshot>)=>void][]=[
  ['unknown profile scope without a facet',snapshot=>{
   delete snapshot.observedInputs;
   snapshot.profile.observedInputScope='observed-build-inputs-v2';
  }],
  ['profile scope omitted while a facet is present',snapshot=>{
   delete snapshot.profile.observedInputScope;
  }],
  ['non-CPU observed backend',snapshot=>{snapshot.profile.buildBackend='cuda';}],
  ['dependency graph claimed complete',snapshot=>{snapshot.profile.dependenciesComplete=true;}],
  ['dependency completion omitted',snapshot=>{delete snapshot.profile.dependenciesComplete;}],
  ['dependency completion stringified',snapshot=>{snapshot.profile.dependenciesComplete='false';}]
 ];
 for(const [label,mutate] of cases)assertRejected(label,mutate);
});

test('rejects current observations that conflict with build identity',()=>{
 const cases:[string,(snapshot:ReturnType<typeof validSnapshot>)=>void][]=[
  ['not configured',snapshot=>{snapshot.configured=false;}],
  ['active build present',snapshot=>{snapshot.activeBuildId=BUILD_ID;}],
  ['changed inputs present',snapshot=>{snapshot.changedInputs=['CMakeLists.txt'];}],
  ['successful manifest missing',snapshot=>{delete snapshot.lastSuccessfulBuild;}],
  ['manifest profile mismatch',snapshot=>{snapshot.lastSuccessfulBuild.profileId='another-profile';}],
  ['manifest source root mismatch',snapshot=>{snapshot.lastSuccessfulBuild.managedSourceRoot='C:/work/other';}],
  ['binary missing',snapshot=>{snapshot.binaryState='missing';}],
  ['binary needs build',snapshot=>{snapshot.binaryState='needs-build';}]
 ];
 for(const [label,mutate] of cases)assertRejected(label,mutate);
});

test('accepts a historical manifest session ID when source root and profile still bind',()=>{
 const snapshot=validSnapshot();
 snapshot.projectId='production-reopen';
 snapshot.latestResult.projectId='production-reopen';
 snapshot.lastSuccessfulBuild.projectId='production';
 assert.equal(snapshot.lastSuccessfulBuild.managedSourceRoot,snapshot.profile.managedSourceRoot);
 assert.equal(snapshot.lastSuccessfulBuild.profileId,snapshot.profile.id);
 assert.equal(validateBuildSnapshot(snapshot,snapshot.projectId),snapshot);
});

test('retains original snapshot refusals and binary-state vocabulary',()=>{
 for(const binaryState of ['missing','available','built-from-current-tracked-inputs','needs-build','freshness-unknown']){
  const snapshot=legacySnapshot();
  snapshot.binaryState=binaryState;
  assert.equal(validateBuildSnapshot(snapshot,PROJECT_ID),snapshot,binaryState);
 }

 const cases:[string,(snapshot:ReturnType<typeof legacySnapshot>)=>void][]=[
  ['project mismatch',snapshot=>{snapshot.projectId='another-project';}],
  ['protocol mismatch',snapshot=>{snapshot.protocolVersion='1.2';}],
  ['new binary state',snapshot=>{snapshot.binaryState='stale';}],
  ['unknown binary state',snapshot=>{snapshot.binaryState='not-built';}],
  ['result from another project',snapshot=>{snapshot.latestResult.projectId='another-project';}],
  ['nonterminal result state',snapshot=>{snapshot.latestResult.state='queued';}],
  ['non-string result error',snapshot=>{snapshot.latestResult.error={message:'failed'};}]
 ];
 for(const [label,mutate] of cases){
  const snapshot=legacySnapshot();
  mutate(snapshot);
  assert.throws(()=>validateBuildSnapshot(snapshot,PROJECT_ID),Error,label);
 }
});

const windowsTools={ninja:'C:/tools/ninja.exe',rc:'C:/tools/rc.exe'};

test('localCpuProfile does not opt in without Windows tools',()=>{
 const {build}=localCpuProfile(SOURCE_ROOT);
 assert.equal(build.id,LOCAL_CPU_PROFILE_ID);
 assert.equal(build.dependenciesComplete,false);
 assert.equal(build.observedInputScope,undefined);
 assert.equal(build.buildBackend,undefined);
});

if(process.platform!=='win32') test('localCpuProfile does not opt in on Linux even with Windows tools',{skip:process.platform==='win32'},()=>{
 const {build}=localCpuProfile(SOURCE_ROOT,windowsTools);
 assert.equal(build.observedInputScope,undefined);
 assert.equal(build.buildBackend,undefined);
});

if(process.platform==='win32') test('localCpuProfile opts into CPU observations only on native Windows with tools',{skip:process.platform!=='win32'},()=>{
 const {build}=localCpuProfile(SOURCE_ROOT,windowsTools);
 assert.equal(build.buildBackend,'cpu');
 assert.equal(build.observedInputScope,OBSERVED_SCOPE);
 assert.equal(build.dependenciesComplete,false);

 const snapshot=validSnapshot();
 snapshot.profile=build;
 snapshot.lastSuccessfulBuild.profileId=build.id;
 snapshot.lastSuccessfulBuild.managedSourceRoot=build.managedSourceRoot;
 assert.equal(validateBuildSnapshot(snapshot,PROJECT_ID),snapshot);
});
