import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import h5 from 'h5wasm/node';
import {inspectPlotfileMetadataIsolated,readPlotfileFieldSliceIsolated,validateIsolatedPlotfileResult,PlotfileReadError} from '../host/isolatedPlotfileMetadata.ts';

const fixture=new URL('./fixtures/sod-1d.h5',import.meta.url).pathname;
const hasCode=(code:string)=>(error:unknown)=>error instanceof PlotfileReadError&&error.code===code;

// Deterministic JSON parser fixture only. Its provenance-shaped values are not
// a producer receipt, a new HDF5 file, or scientific qualification evidence.
function formalResponse(dimension:number,geometry='cartesian'){
 const sha=(character:string)=>character.repeat(64);
 const source={version:'arch-plot-identity-1',scope:'resolved-runtime',caseId:'ParserFixture',
  caseSource:'ConfigurationInput.case_id',caseSourceSha256:sha('c'),rawConfigSha256:sha('a'),
  rawConfigSource:'ConfigurationInput.raw_text; exact parser bytes',binarySha256:sha('b'),
  binarySource:'Linux /proc/self/exe',binaryScope:'main-executable-only',eosType:'ideal',
  eosSource:'resolved-runtime-checkpoint-provenance',eosTableState:'not-applicable',eosTableSha256:null,idealGamma:1.4,
  speciesState:'recorded',speciesNames:[] as string[],speciesProperties:{version:'checkpoint-species-1',state:'recorded',
   source:'resolved-runtime-checkpoint-provenance',values:{A:[] as number[],Z:[] as number[],gamma:[] as number[],Cv:[] as number[]}},
  runId:'67cd09d8-a208-4ff8-92ad-cd66d58f207f',runIdSource:'DriverIO output session; OS-generated UUIDv4',
  effectiveConfigVersion:'arch-effective-configuration-1',effectiveConfigSha256:sha('d'),
  effectiveConfigSource:'immutable-runtime-config-and-resolved-plan',buildIdentityVersion:'arch-build-identity-1',
  buildIdentityScope:'project-source-and-configured-compiler-profile',buildSource:'CMake-embedded-project-source-and-compiler-profile',
  buildId:sha('e'),sourceManifestSha256:sha('f'),buildProfileSha256:sha('0'),sourceGitHead:null,
  sourceGitDirty:'unknown',sourceGitSource:'optional-build-time-annotation',eosIdentityVersion:'arch-eos-identity-1',
  eosIdentitySha256:sha('1'),eosTableIdentityKind:'not-applicable',resolvedBackend:'cpu',recordsVerified:true,eosUnitSystem:'cgs'};
 const axes=geometry==='cartesian'?['x','y','z']:dimension===3?
  (geometry==='cylindrical'?['r','z','phi']:['r','theta','phi']):['r','phi','inactive'];
 const axisUnits=axes.map((axis,index)=>index>=dimension?'inactive':['theta','phi'].includes(axis)?'rad':'cm');
 for(let index=dimension;index<3;index++)axes[index]='inactive';
 const cellShape=Array<number>(dimension).fill(2);
 return {schemaVersion:'audit-1',file:{bytes:4096,sha256:sha('2')},time:0,dimension,geometry,blocks:1,
  cellShape,cells:2**dimension,fields:[{name:'DENS',shape:[1,...cellShape],unit:null}],timeUnit:'s',
  coordinates:{storedBasis:'cartesian',centering:'cell-center',units:'cm'},
  candidateSourceIdentity:source,candidateNativeGrid:{
   version:geometry==='cartesian'?'arch-native-cartesian-1':'arch-native-curvilinear-1',
   measureSource:'GridMetrics::CellVolume',measureConvention:geometry==='cartesian'?
    'active-coordinate-product; inactive-measures-omitted':'GridMetrics-native-coordinate-integral',
   measureUnit:dimension===1?'cm':dimension===2?'cm^2':'cm^3',measureNormalization:dimension===1?
    'per_unit_transverse_area':dimension===2?'per_unit_transverse_length':'full_volume',geometry,axes,axisUnits,chart:'existing'},
  completion:{state:'complete',reason:'Synthetic parser fixture, not a publication receipt.'},
  scientificIdentity:{case:source.caseId,config:source.effectiveConfigSha256,build:source.buildId,
   binary:source.binarySha256,eos:source.eosIdentitySha256},nativeCellGeometry:{bounds:'recorded',volume:'recorded'},
  renderEligible:geometry==='cartesian'&&[1,2].includes(dimension)};
}

test('isolated response gate accepts formal Cartesian display and curved/3D Inspector semantics',()=>{
 for(const [dimension,geometry,render] of [[1,'cartesian',true],[2,'cartesian',true],
  [3,'cartesian',false],[2,'cylindrical',false],[3,'spherical',false]] as const){
  const response=formalResponse(dimension,geometry),before=JSON.stringify(response);
  const result=validateIsolatedPlotfileResult(response);
  assert.equal(result.renderEligible,render);assert.equal(result.completion.state,'complete');
  assert.equal(JSON.stringify(response),before,'validation must not rewrite worker evidence');
  // Full canonical record text remains in the file, not this bounded response.
  assert.ok(Buffer.byteLength(JSON.stringify({ok:true,result:response}))<64*1024);
 }
});

test('isolated bounded formal identity response retains complete constituent metadata',context=>{
 const response=formalResponse(2),source=response.candidateSourceIdentity;
 // Exercise the existing reader/client limits with deterministic short names;
 // arbitrarily long field text remains subject to the unchanged 64 KiB cap.
 source.speciesNames=Array.from({length:128},(_,index)=>'species-'+index);
 source.speciesProperties.values={A:Array<number>(128).fill(1),Z:Array<number>(128).fill(1),
  gamma:Array<number>(128).fill(1.4),Cv:Array<number>(128).fill(2)};
 response.fields=Array.from({length:128},(_,index)=>({name:'FIELD_'+index,shape:[1,2,2],unit:null}));
 const bytes=Buffer.byteLength(JSON.stringify({ok:true,result:response}));
 assert.ok(bytes<64*1024);validateIsolatedPlotfileResult(response);
 assert.equal(source.speciesProperties.values.Cv.length,128);
 context.diagnostic('Complete deterministic 128-species/128-field JSON response: '+bytes+' bytes; cap remains 65536.');
});

test('isolated response gate rejects completion, render and source/native identity contradictions',()=>{
 const response=formalResponse(2);
 for(const invalid of [
  {...response,completion:{state:'unknown',reason:'not published'}},
  {...response,completion:{state:'incomplete',reason:'not closed'}},
  {...response,renderEligible:false},
  {...response,scientificIdentity:{...response.scientificIdentity,binary:'3'.repeat(64)}},
  {...response,scientificIdentity:{...response.scientificIdentity,eos:null}},
  {...response,candidateSourceIdentity:{...response.candidateSourceIdentity,recordsVerified:false}},
  {...response,candidateSourceIdentity:{...response.candidateSourceIdentity,binarySha256:'not-a-digest'}},
  {...response,candidateSourceIdentity:{...response.candidateSourceIdentity,eosUnitSystem:null}},
  {...response,candidateNativeGrid:null},
  {...response,candidateNativeGrid:{...response.candidateNativeGrid,version:'candidate-cartesian-1'}},
  {...response,candidateNativeGrid:{...response.candidateNativeGrid,geometry:'spherical'}},
  {...response,nativeCellGeometry:{bounds:'unavailable',volume:'recorded'}},
  {...formalResponse(3),renderEligible:true},
  {...formalResponse(2,'cylindrical'),renderEligible:true},
 ])assert.throws(()=>validateIsolatedPlotfileResult(invalid));
});

test('isolated response gate preserves legacy unknown and refuses invented completion',async()=>{
 const legacy=await inspectPlotfileMetadataIsolated(fixture);
 assert.equal(validateIsolatedPlotfileResult(legacy).completion.state,'unknown');
 for(const invalid of [
  {...legacy,completion:{state:'complete',reason:'fabricated'}},
  {...legacy,renderEligible:true},
  {...legacy,scientificIdentity:{...legacy.scientificIdentity,case:'ParserFixture'}},
 ])assert.throws(()=>validateIsolatedPlotfileResult(invalid));
});

test('isolated formal slice gate keeps exact indices, native bounds and raw FP64 values',()=>{
 const response=formalResponse(1),selection={field:'DENS',block:0,start:[0],count:[1]};
 const payload={field:'DENS',block:0,start:[0],shape:[1],order:'x1-fastest',linearIndices:[0],
  values:[1+Number.EPSILON],coordinates:{x:[.25],y:[0],z:[0]},unit:null,diagnostics:[],
  nativeCells:{...response.candidateNativeGrid,identityScope:'file-local',logicalKey:'0/0/0/0',level:0,
   logicalCoordinates:[0,0,0],lower:{x1:[0],x2:[0],x3:[0]},upper:{x1:[.5],x2:[0],x3:[0]},cellMeasure:[.5]}};
 const result={...response,schemaVersion:'audit-slice-1',payload};
 assert.equal(validateIsolatedPlotfileResult(result,selection).payload?.values[0],1+Number.EPSILON);
 for(const invalid of [
  {...result,payload:{...payload,linearIndices:[1]}},
  {...result,payload:{...payload,nativeCells:{...payload.nativeCells,cellMeasure:[-1]}}},
  {...result,payload:{...payload,nativeCells:{...payload.nativeCells,upper:{...payload.nativeCells.upper,x1:[0]}}}},
  {...result,payload:{...payload,values:[Number.NaN]}},
  {...result,schemaVersion:'audit-1'},
 ])assert.throws(()=>validateIsolatedPlotfileResult(invalid,selection));
});

test('isolated actual metadata stays read-only and truthful',async()=>{
 const before=await readFile(fixture);
 const result=await inspectPlotfileMetadataIsolated(fixture);
 assert.equal(result.dimension,1);assert.equal(result.time,.15);
 assert.equal(result.renderEligible,false);assert.equal(result.completion.state,'unknown');
 assert.deepEqual(await readFile(fixture),before);
});
test('pre-cancel does not occupy a worker; malformed input recovers',async()=>{
 const controller=new AbortController();controller.abort();
 await assert.rejects(inspectPlotfileMetadataIsolated(fixture,{signal:controller.signal}),hasCode('CANCELLED'));
 await assert.rejects(inspectPlotfileMetadataIsolated('/definitely/missing/ARCH-plot.h5'),hasCode('WORKER_FAILED'));
 assert.equal((await inspectPlotfileMetadataIsolated(fixture)).dimension,1);
});
test('timeout and in-flight cancellation reap worker before releasing capacity',async()=>{
 await assert.rejects(inspectPlotfileMetadataIsolated(fixture,{timeoutMs:1}),hasCode('TIMEOUT'));
 const controller=new AbortController();
 const pending=inspectPlotfileMetadataIsolated(fixture,{signal:controller.signal});
 await assert.rejects(inspectPlotfileMetadataIsolated(fixture),hasCode('BUSY'));
 controller.abort();
 await assert.rejects(pending,hasCode('CANCELLED'));
 assert.equal((await inspectPlotfileMetadataIsolated(fixture)).renderEligible,false);
});
test('invalid Host timeout is rejected without acquiring capacity',async()=>{
 for(const timeoutMs of [0,-1,15_001,NaN,1.5])
  await assert.rejects(inspectPlotfileMetadataIsolated(fixture,{timeoutMs}),RangeError);
 assert.equal((await inspectPlotfileMetadataIsolated(fixture)).dimension,1);
});

test('oversized real HDF5 metadata response terminates and releases worker capacity',async()=>{
 await h5.ready;
 const dir=await mkdtemp(join(tmpdir(),'arch-plt-isolation-'));
 try{
  const path=join(dir,'large-headers.h5');
  const file=new h5.File(path,'w');
  try{
   file.create_attribute('time',0);file.create_attribute('dim',1);file.create_attribute('geometry','cartesian');
   const grid=file.create_group('Grid');
   for(const name of ['x','y','z','level','morton'])grid.create_dataset({name,data:new Float64Array(1)});
   const data=file.create_group('Data');
   data.create_dataset({name:'DENS',data:new Float64Array(1),shape:[1,1]});
   for(let i=0;i<80;i++)data.create_dataset({name:'FIELD_'+i+'_'+('a'.repeat(1000)),data:new Float64Array(1),shape:[1,1]});
  }finally{file.close();}
  await assert.rejects(inspectPlotfileMetadataIsolated(path),hasCode('OUTPUT_LIMIT'));
  await assert.rejects(readPlotfileFieldSliceIsolated(path,{field:'DENS',block:0,start:[0],count:[1]}),hasCode('OUTPUT_LIMIT'));
  assert.equal((await inspectPlotfileMetadataIsolated(fixture)).dimension,1);
 }finally{await rm(dir,{recursive:true,force:true});}
});

test('isolated slice keeps truthful identity and shares cancellation/capacity with metadata',async()=>{
 const request={field:'DENS',block:0,start:[0],count:[8]};
 const before=await readFile(fixture);
 const result=await readPlotfileFieldSliceIsolated(fixture,request);
 assert.equal(result.schemaVersion,'audit-slice-1');assert.equal(result.payload?.values.length,8);
 assert.deepEqual(result.payload?.linearIndices,[0,1,2,3,4,5,6,7]);
 assert.equal(result.renderEligible,false);assert.equal(result.scientificIdentity.case,null);
 assert.deepEqual(await readFile(fixture),before);
 const controller=new AbortController();
 const pending=readPlotfileFieldSliceIsolated(fixture,request,{signal:controller.signal});
 await assert.rejects(inspectPlotfileMetadataIsolated(fixture),hasCode('BUSY'));
 controller.abort();await assert.rejects(pending,hasCode('CANCELLED'));
 assert.equal((await inspectPlotfileMetadataIsolated(fixture)).schemaVersion,'audit-1');
 await assert.rejects(readPlotfileFieldSliceIsolated(fixture,request,{timeoutMs:1}),hasCode('TIMEOUT'));
 assert.equal((await readPlotfileFieldSliceIsolated(fixture,request)).payload?.values.length,8);
});
test('slice invalid requests reject before acquiring worker and cannot supply execution settings',async()=>{
 const valid={field:'DENS',block:0,start:[0],count:[8]};
 for(const request of [
  {...valid,count:[513]}, {...valid,block:-1}, {...valid,start:[NaN]},
  {...valid,start:[0,0]}, {...valid,program:'sh'}, {...valid,env:{}},
 ])await assert.rejects(readPlotfileFieldSliceIsolated(fixture,request));
 const controller=new AbortController();controller.abort();
 await assert.rejects(readPlotfileFieldSliceIsolated(fixture,valid,{signal:controller.signal}),hasCode('CANCELLED'));
 assert.equal((await inspectPlotfileMetadataIsolated(fixture)).dimension,1);
});
