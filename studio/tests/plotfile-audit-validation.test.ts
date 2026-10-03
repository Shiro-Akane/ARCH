import {test} from 'node:test';
import assert from 'node:assert/strict';
import {inspectPlotfileMetadata,readPlotfileFieldSlice} from '../host/plotfileMetadata.ts';
import {validatePlotfileAudit} from '../src/host/plotfileAudit.ts';
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
