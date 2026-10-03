import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
import test from 'node:test';
import assert from 'node:assert/strict';
import {realInitSlice,realSampleCoordinates,gridPoint,RealInitPreviewProvider} from '../src/data/RealInitPreviewProvider.ts';
import {previewProfileForConfiguration} from '../src/data/previewProfileSelection.ts';
import type {RealPreviewResult,PreviewProfile} from '../src/host/previewContracts.ts';
import type {InspectionResponse} from '../src/host/configurationContracts.ts';
function volume():RealPreviewResult{
 const values=Array.from({length:24},(_,index)=>{const i=index%4,j=Math.floor(index/4)%3,k=Math.floor(index/12);return 100*k+10*j+i;});
 return {core:{data:{dimension:3,kind:'volume',sampling:{shape:[2,3,4],count:24},
  axes:[{name:'x1',unit:'cm',values:[1,3,5,7]},{name:'x2',unit:'rad',values:[.1,.3,.5]},{name:'x3',unit:'rad',values:[.2,.6]}],
  coordinates:{metadata:{axes:[{displayName:'r'},{displayName:'theta'},{displayName:'phi'}]}},
  fields:[{key:'DENS',displayName:'Density',unit:'g/cm^3',values,min:0,max:123}]}}} as unknown as RealPreviewResult;
}
test('non-cubic volume slices preserve global raw indices on all three native planes',()=>{
 const result=volume(),before=JSON.stringify(result);
 const xy=realInitSlice(result,'DENS',2,1);
 assert.deepEqual([xy.width,xy.height],[4,3]);
 assert.deepEqual(Array.from(xy.values),[100,101,102,103,110,111,112,113,120,121,122,123]);
 assert.deepEqual([xy.xName,xy.yName,xy.xUnit,xy.yUnit],['r','theta','cm','rad']);
 const xz=realInitSlice(result,'DENS',1,1);
 assert.deepEqual([xz.width,xz.height],[4,2]);
 assert.deepEqual(Array.from(xz.values),[10,11,12,13,110,111,112,113]);
 const yz=realInitSlice(result,'DENS',0,2);
 assert.deepEqual([yz.width,yz.height],[3,2]);
 assert.deepEqual(Array.from(yz.values),[2,12,22,102,112,122]);
 const hit=gridPoint(yz,.3,.6)!;
 assert.equal(yz.globalIndices![hit.index],18);
 assert.deepEqual(realSampleCoordinates(result,18).map(c=>[c.name,c.index,c.value,c.unit]),
  [['r',2,5,'cm'],['theta',1,.3,'rad'],['phi',1,.6,'rad']]);
 // A display buffer is independent; the Inspector always addresses original fields.
 yz.values[hit.index]=-900;
 assert.equal(result.core.data!.fields[0].values[18],112);
 assert.equal(JSON.stringify(result),before);
 for(const args of [[3,0],[1,3],[0,-1],[.5,0]])assert.throws(()=>realInitSlice(result,'DENS',args[0],args[1]));
 assert.throws(()=>realSampleCoordinates(result,24));
});

test('configuration dimension/profile selection rejects stale input and every build-scope mismatch',()=>{
 const inspection={identity:{projectId:'p',caseId:'Gaussian',buildId:'b',binarySha256:'sha'},core:{coordinates:{dimension:3}}} as InspectionResponse;
 const profiles=[{id:'1d',caseId:'Gaussian',dimension:1},{id:'3d',caseId:'Gaussian',dimension:3}] as PreviewProfile[];
 const select=(value=inspection,text='current',inspected='current',project='p',model='Gaussian',build='b',sha='sha')=>
  previewProfileForConfiguration(profiles,value,text,inspected,project,model,build,sha)?.id;
 assert.equal(select(),'3d');
 assert.equal(select(inspection,'edited'),undefined);
 assert.equal(select(inspection,'current','old'),undefined);
 assert.equal(select(inspection,'current','current','other'),undefined);
 assert.equal(select(inspection,'current','current','p','Sod'),undefined);
 assert.equal(select(inspection,'current','current','p','Gaussian','new'),undefined);
 assert.equal(select(inspection,'current','current','p','Gaussian','b','changed'),undefined);
 const invalid=structuredClone(inspection);invalid.core.coordinates!.dimension=0;
 assert.equal(select(invalid),undefined);
});

test('generic provider requires the selected case identity without browser-supplied case commands',async()=>{
 const provider=new RealInitPreviewProvider();
 await assert.rejects(provider.start('p','unsaved','initial-cpu-Gaussian-3d',[2,3,4]),/runtime case identity/);
 const original=globalThis.fetch;let submitted:Record<string,unknown>|undefined;
 globalThis.fetch=async(_url,init)=>{
  submitted=JSON.parse(String(init?.body));
  return new Response(JSON.stringify({protocolVersion:PROTOCOL_VERSION,projectId:'p',
   requestId:'00000000-0000-0000-0000-000000000001',
   identity:{requestId:'00000000-0000-0000-0000-000000000001',projectId:'p',
    profileId:submitted!.profileId,caseId:'Sod',configRevision:submitted!.configRevision,buildId:'b',binarySha256:'a'.repeat(64)}}));
 };
 try{
  await assert.rejects(provider.start('p','unsaved','initial-cpu-Gaussian-3d',[2,3,4],'Gaussian'),/acceptance identity/);
  assert.equal(submitted?.caseId,undefined);assert.equal(submitted?.program,undefined);
  assert.deepEqual(submitted?.requestedShape,[2,3,4]);
 }finally{globalThis.fetch=original;}
});
