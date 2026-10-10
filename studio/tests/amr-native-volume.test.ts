import test from 'node:test';import assert from 'node:assert/strict';import {readFileSync} from 'node:fs';
import {validateMesh,validateWorkflowCore} from '../src/host/workflowValidation.ts';
import {amrPlaneAxes,amrPlaneLeaves,amrAt,amrAxisUnit} from '../src/data/amrGeometry.ts';
import {matchingAmrField} from '../src/data/amrIdentity.ts';
import type {AmrMesh,WorkflowResult} from '../src/host/workflowContracts.ts';
import type {RealPreviewResult} from '../src/host/previewContracts.ts';
const example=()=>JSON.parse(readFileSync(new URL('../../src/api/examples/local-workflow/sod-mesh.json',import.meta.url),'utf8'));
function mesh3(){
 const m=example().data;
 m.dimension=3;m.geometry='spherical';m.unit=null;
 m.coordinates={version:'1',basis:'native-grid',metadata:{geometry:'spherical',dimension:3,unitSystem:'cgs',
  axes:['r','theta','phi'].map((name,i)=>({key:'x'+(i+1),displayName:name,nativeName:name,active:true,
   kind:i?'angular':'radial',unit:i?'rad':'cm',blocks:i===2?2:1,blocksKey:'nblockx'+(i+1),
   minKey:'x'+(i+1)+'_min',maxKey:'x'+(i+1)+'_max',lowerBoundaryKey:'bc_x'+(i+1)+'_lo',upperBoundaryKey:'bc_x'+(i+1)+'_hi'}))}};
 m.resources.dimension=3;m.leafCount=2;m.levelCounts=[{level:0,leafBlocks:2}];
 m.leaves=[0,1].map(k=>({logicalKey:'0:0:0:'+k,level:0,logicalIndex:[0,0,k],
  lower:[1,.1,k],upper:[3,.5,k+1],cellShape:[4,2,2],cellSpacing:[.5,.2,.5]}));
 return m;
}
function nativeRoot(refine:boolean){
 const m=mesh3();m.dimension=2;m.geometry='cylindrical';m.resources.dimension=2;
 m.coordinates.metadata.dimension=2;m.coordinates.metadata.geometry='cylindrical';
 m.coordinates.metadata.axes.forEach((a:Record<string,unknown>,i:number)=>{
  a.displayName=a.nativeName=['r','z','phi'][i];a.active=i<2;
  a.kind=['radial','linear','angular'][i];a.unit=i===2?'rad':'cm';a.blocks=i===0?1:i===1?2:0;
 });
 m.leaves=[0,1].map(z=>({logicalKey:'0:0:'+z+':0',level:0,logicalIndex:[0,z,0],
  lower:[0,z],upper:[1,z+1],cellShape:[4,4],cellSpacing:[.25,.25]}));
 m.levelCounts=[{level:0,leafBlocks:2}];m.completedPasses=0;m.configuredMaxLevel=refine?1:0;
 m.snapshot='native-root-topology-active-initialization-only';m.complete=!refine;
 m.limitedReason=refine?'native-rz-amr-preview-not-qualified':null;
 return m;
}
test('Native RZ root initialization is complete only without requested refinement and remains a typed limited snapshot otherwise',()=>{
 for(const refine of [false,true]){
  const core=example();core.data=nativeRoot(refine);core.status=refine?'limited':'ok';
  core.state.grid.dimension=2;core.state.grid.geometry='cylindrical';
  const accepted=validateWorkflowCore(core,'preview-amr',core.identity);
  assert.equal(accepted.status,refine?'limited':'ok');
  assert.equal((accepted.data as AmrMesh).complete,!refine);
  assert.equal((accepted.data as AmrMesh).completedPasses,0);
  assert.equal((accepted.data as AmrMesh).snapshot,'native-root-topology-active-initialization-only');
  assert.equal((accepted.data as AmrMesh).limitedReason,refine?'native-rz-amr-preview-not-qualified':null);
 }
 for(const mutate of [
  (m:ReturnType<typeof nativeRoot>)=>m.snapshot='unrecognized-snapshot',
  (m:ReturnType<typeof nativeRoot>)=>m.completedPasses=1,
  (m:ReturnType<typeof nativeRoot>)=>{m.leaves[0].level=1;m.leaves[0].logicalKey='1:0:0:0';},
  (m:ReturnType<typeof nativeRoot>)=>m.geometry='spherical',
  (m:ReturnType<typeof nativeRoot>)=>m.limitedReason='regrid-exceeds-working-capacity',
 ]){const m=nativeRoot(true);mutate(m);assert.throws(()=>validateMesh(m,'limited'));}
 const requested=nativeRoot(true);requested.complete=true;requested.limitedReason=null;
 assert.throws(()=>validateMesh(requested,'ok'));
 const rootOnly=nativeRoot(false);rootOnly.complete=false;rootOnly.limitedReason='native-rz-amr-preview-not-qualified';
 assert.throws(()=>validateMesh(rootOnly,'limited'));
});
test('native three-axis AMR mesh requires authoritative matching coordinates, resources and state',()=>{
 assert.equal(validateMesh(mesh3(),'ok').dimension,3);
 for(const mutate of [(m:ReturnType<typeof mesh3>)=>delete m.coordinates,
  (m:ReturnType<typeof mesh3>)=>m.coordinates.metadata.dimension=2,
  (m:ReturnType<typeof mesh3>)=>m.coordinates.metadata.geometry='cartesian',
  (m:ReturnType<typeof mesh3>)=>m.coordinates.metadata.axes[2].active=false,
  (m:ReturnType<typeof mesh3>)=>m.resources.dimension=2,
  (m:ReturnType<typeof mesh3>)=>m.unit='cm',
  (m:ReturnType<typeof mesh3>)=>m.leaves[0].cellSpacing[2]=99]){
  const m=mesh3();mutate(m);assert.throws(()=>validateMesh(m,'ok'));
 }
 const core=example();core.data=mesh3();core.state.grid.dimension=2;core.state.grid.geometry='spherical';
 assert.throws(()=>validateWorkflowCore(core,'preview-amr',core.identity),/grid disagrees/);
});
test('three-plane leaf intersection uses half-open internal boundary, outer-face inclusion and original block identity',()=>{
 const mesh=validateMesh(mesh3(),'ok'),before=JSON.stringify(mesh),levels=new Set([0]);
 assert.deepEqual(amrPlaneAxes(mesh,{axis:0,coordinate:2}),[1,2]);
 assert.deepEqual(amrPlaneAxes(mesh,{axis:1,coordinate:.3}),[0,2]);
 assert.deepEqual(amrPlaneAxes(mesh,{axis:2,coordinate:1}),[0,1]);
 assert.equal(amrPlaneLeaves(mesh,{axis:2,coordinate:1})[0],mesh.leaves[1]);
 assert.equal(amrPlaneLeaves(mesh,{axis:2,coordinate:2})[0],mesh.leaves[1]);
 assert.equal(amrAt(mesh,2,.3,levels,{axis:2,coordinate:.5})?.logicalKey,'0:0:0:0');
 assert.equal(amrAt(mesh,2,.3,levels,{axis:2,coordinate:1})?.logicalKey,'0:0:0:1');
 assert.equal(amrAt(mesh,.3,1.5,levels,{axis:0,coordinate:2})?.logicalKey,'0:0:0:1');
 assert.equal(amrAt(mesh,2,1.5,levels,{axis:1,coordinate:.3})?.logicalKey,'0:0:0:1');
 assert.equal(amrAt(mesh,2,.3,new Set(),{axis:2,coordinate:1}),undefined);
 assert.deepEqual(amrPlaneLeaves(mesh),[]);assert.deepEqual(amrPlaneLeaves(mesh,{axis:2,coordinate:3}),[]);
 assert.equal(amrAxisUnit(mesh,1),'rad');assert.equal(JSON.stringify(mesh),before);
});
test('field/AMR matching additionally requires native geometry and per-axis units',()=>{
 const data=validateMesh(mesh3(),'ok'),core=example();core.data=data;
 const identity={projectId:'p',caseId:'Gaussian',buildId:'b',binarySha256:'sha',configRevision:'rev',requestId:'mesh'};
 const mesh={identity,operation:'preview-amr',core} as WorkflowResult;
 const field={identity:{...identity,requestId:'field'},core:{data:{dimension:3,coordinates:{geometry:'spherical',metadata:structuredClone(data.coordinates!.metadata)}},state:{eos:structuredClone(core.state.eos)}}} as unknown as RealPreviewResult;
 assert.equal(matchingAmrField(mesh,field),true);
 const changed=structuredClone(field);changed.core.data!.coordinates!.geometry='cylindrical';
 assert.equal(matchingAmrField(mesh,changed),false);
 changed.core.data!.coordinates!.geometry='spherical';changed.core.data!.coordinates!.metadata.axes[1].unit='cm';
 assert.equal(matchingAmrField(mesh,changed),false);
 delete changed.core.data!.coordinates;assert.equal(matchingAmrField(mesh,changed),false);
 const noUnits=structuredClone(data) as AmrMesh;noUnits.coordinates!.metadata.axes[0].unit=null;
 assert.equal(amrAxisUnit(noUnits,0),null);
});
