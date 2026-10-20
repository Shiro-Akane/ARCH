import {test} from 'node:test';
import assert from 'node:assert/strict';
import {inspectPlotfileMetadata,readPlotfileFieldSlice} from '../host/plotfileMetadata.ts';
import {storedCellIndices,validatePlotfileAudit} from '../src/host/plotfileAudit.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
const path=new URL('./fixtures/sod-1d.h5',import.meta.url).pathname;
const identity={protocolVersion:PROTOCOL_VERSION,projectId:'session',relativePath:'result.h5'};
test('client accepts actual reader evidence and rejects fabricated completion, units and project association',async()=>{
 const metadata=await inspectPlotfileMetadata(path),response={...identity,metadata};
 assert.equal(validatePlotfileAudit(response,'session','result.h5').audit.renderEligible,false);
 for(const invalid of [
  {...response,projectId:'other'}, {...response,relativePath:'different.h5'},
  {...response,metadata:{...metadata,renderEligible:true}},
  {...response,metadata:{...metadata,completion:{state:'complete',reason:'guess'}}},
  {...response,metadata:{...metadata,coordinates:{...metadata.coordinates,units:'cm'}}},
  {...response,metadata:{...metadata,scientificIdentity:{...metadata.scientificIdentity,build:'unverified-build'}}},
  {...response,metadata:{...metadata,cells:metadata.cells+1}},
 ])assert.throws(()=>validatePlotfileAudit(invalid,'session','result.h5'));
});
test('client slice validation locks request shape, file SHA and raw coordinate/value lengths',async()=>{
 const selection={field:'PRES',block:2,start:[0],count:[1]},result=await readPlotfileFieldSlice(path,selection);
 const response={...identity,result},sha=result.file.sha256;
 assert.deepEqual(validatePlotfileAudit(response,'session','result.h5',selection,sha).audit.payload?.values,[0.3054751636143017]);
 for(const invalid of [
  {...response,result:{...result,file:{...result.file,sha256:'0'.repeat(64)}}},
  {...response,result:{...result,payload:{...result.payload,shape:[2]}}},
  {...response,result:{...result,payload:{...result.payload,values:[null]}}},
  {...response,result:{...result,payload:{...result.payload,linearIndices:[33]}}},
  {...response,result:{...result,payload:{...result.payload,coordinates:{x:[],y:[0],z:[0]}}}},
 ])assert.throws(()=>validatePlotfileAudit(invalid,'session','result.h5',selection,sha));
});


test('client validates native identity/bounds/measure and refuses native data without matching candidate header',async()=>{
 const selection={field:'PRES',block:2,start:[0],count:[1]},result=await readPlotfileFieldSlice(path,selection);
 const header={version:'candidate-cartesian-1',measureSource:'GridMetrics::CellVolume',
  measureConvention:'active-coordinate-product; inactive-measures-omitted',measureUnit:null};
 const native={...header,identityScope:'file-local',logicalKey:'0/2/0/0',level:0,logicalCoordinates:[2,0,0],
  lower:{x1:[.5],x2:[0],x3:[0]},upper:{x1:[.515625],x2:[0],x3:[0]},cellMeasure:[.015625]};
 const response={...identity,result:{...result,candidateNativeGrid:header,payload:{...result.payload,nativeCells:native}}};
 const sha=result.file.sha256;
 assert.equal(validatePlotfileAudit(response,'session','result.h5',selection,sha).audit.payload?.nativeCells?.logicalKey,'0/2/0/0');
 for(const change of [
  {logicalKey:'0/3/0/0'}, {identityScope:'global'}, {logicalCoordinates:[2,-1,0]},
  {level:NaN}, {measureUnit:'cm^3'}, {measureSource:'frontend guessed'},
  {cellMeasure:[null]}, {cellMeasure:[0]}, {cellMeasure:[Infinity]},
  {lower:{x1:[],x2:[0],x3:[0]}}, {upper:{x1:[.5],x2:[0],x3:[0]}},
  {upper:{x1:[.515625],x2:[1],x3:[0]}},
 ]){
  const invalid={...response,result:{...response.result,payload:{...response.result.payload,nativeCells:{...native,...change}}}};
  assert.throws(()=>validatePlotfileAudit(invalid,'session','result.h5',selection,sha),/native/);
 }
 assert.throws(()=>validatePlotfileAudit({...response,result:{...response.result,candidateNativeGrid:null}},'session','result.h5',selection,sha),/native/);
 assert.throws(()=>validatePlotfileAudit({...response,result:{...response.result,payload:{...response.result.payload,nativeCells:null}}},'session','result.h5',selection,sha),/native/);
 assert.throws(()=>validatePlotfileAudit({...response,result:{...response.result,candidateNativeGrid:{...header,version:'future'}}},'session','result.h5',selection,sha),/native/);
});
test('Inspector no-ghost indices reverse x1-fastest storage for non-square 2D/3D',()=>{
 assert.deepEqual(storedCellIndices({cellShape:[3,5]},1,21),[1,1,0]);
 assert.deepEqual(storedCellIndices({cellShape:[3,4,5]},1,93),[3,2,1]);
 assert.deepEqual(storedCellIndices({cellShape:[16]},2,32),[0,0,0]);
 assert.throws(()=>storedCellIndices({cellShape:[3,5]},1,14));
 assert.throws(()=>storedCellIndices({cellShape:[3,5]},1,30));
});

import {POINT_BOUNDARY_RULE,validPointEvidence,pointMatchesNativeCell} from '../src/host/plotfilePoint.ts';
test('3D point evidence requires a finite z domain and matches the returned native z bounds',()=>{
 const request={field:'DENS',point:[3.5,2.5,2]};
 const evidence={version:'candidate-native-point-1' as const,...request,rule:POINT_BOUNDARY_RULE,
  domain:{x:[1,3.5] as [number,number],y:[1,2.5] as [number,number],z:[1,2] as [number,number]},scannedCells:30,matchCount:1 as const};
 assert.ok(validPointEvidence(evidence,request,30,3));
 for(const domain of [{x:[1,3.5],y:[1,2.5]}, {...evidence.domain,z:[1,NaN]},
  {...evidence.domain,z:[2,1]}, {...evidence.domain,z:[1,2],world:true}])
  assert.equal(validPointEvidence({...evidence,domain},request,30,3),false);
 assert.equal(validPointEvidence(evidence,{field:'DENS',point:[3.5,2.5]},30,2),false);
 assert.equal(validPointEvidence({...evidence,scannedCells:29},request,30,3),false);
 const native={lower:{x1:[3],x2:[2],x3:[1.5]},upper:{x1:[3.5],x2:[2.5],x3:[2]}};
 assert.ok(pointMatchesNativeCell(native,request,evidence));
 assert.equal(pointMatchesNativeCell({...native,upper:{...native.upper,x3:[1.75]}},request,evidence),false);
 assert.equal(pointMatchesNativeCell(native,request,{...evidence,domain:{...evidence.domain,z:[1,2.5]}}),false);
});
