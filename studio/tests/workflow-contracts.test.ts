import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {validateRegistry,validateWorkflowCore,validateMesh,validateResources} from '../src/host/workflowValidation.ts';
import {amrAt,meshDomain,visibleCellCoordinates} from '../src/data/amrGeometry.ts';
import {matchingAmrField} from '../src/data/amrIdentity.ts';
import type {WorkflowResult} from '../src/host/workflowContracts.ts';
import type {RealPreviewResult} from '../src/host/previewContracts.ts';
const example=(name:string)=>JSON.parse(readFileSync(new URL('../../src/api/examples/local-workflow/'+name,import.meta.url),'utf8'));
test('binary registry distinguishes registration, probe and field/AMR support',()=>{
 const cases=validateRegistry(example('registered-cases.json'));
 assert.equal(cases.length,11);
 assert.equal(cases.filter(c=>c.initialFieldPreview).length,2);
 assert.equal(cases.find(c=>c.caseId==='Gaussian')?.initialFieldPreview,false);
 const broken=example('registered-cases.json');broken.cases.push(broken.cases[0]);assert.throws(()=>validateRegistry(broken));
});
test('real Core examples validate as independent mesh, resource and Init-probe contracts',()=>{
 for(const [name,op] of [['sod-resources.json','amr-resources'],['sod-mesh.json','preview-amr'],['sod-limited.json','preview-amr'],['cellular-mesh.json','preview-amr'],['case-inspection-sod.json','inspect-case'],['case-inspection-cellular.json','inspect-case'],['case-inspection-sedov3d.json','inspect-case'],['case-inspection-invalid-int.json','inspect-case']] as const){
  const v=example(name);assert.equal(validateWorkflowCore(v,op,v.identity).status,v.status);
  assert.throws(()=>validateWorkflowCore(v,op,{...v.identity,configRevision:'wrong'}));
 }
});
test('limited mesh and no snapshot remain distinct from completed zero blocks',()=>{
 const v=example('sod-limited.json');assert.equal(validateMesh(v.data,'limited').complete,false);
 assert.throws(()=>validateMesh(v.data,'ok'));
 v.data.leaves=[];v.data.leafCount=0;v.data.levelCounts=[];v.data.snapshot='none';
 assert.equal(validateMesh(v.data,'limited').snapshot,'none');
 v.data.complete=true;assert.throws(()=>validateMesh(v.data,'ok'));
});
test('mesh geometry and counts reject corrupted hierarchy without mutating original arrays',()=>{
 for(const mutate of [(v:ReturnType<typeof example>)=>v.leaves.push(v.leaves[0]),(v:ReturnType<typeof example>)=>v.leaves[0].cellSpacing[0]*=2,(v:ReturnType<typeof example>)=>v.levelCounts[0].leafBlocks++,(v:ReturnType<typeof example>)=>v.leaves[0].logicalKey='pool:0']){
  const v=example('sod-mesh.json').data;mutate(v);assert.throws(()=>validateMesh(v,'ok'));
 }
});
test('resource overflow is null/unknown, not zero; unsafe int64 remains visibly approximate-capable',()=>{
 const v=example('sod-resources.json').data;
 v.levels[0].overflow=true;v.levels[0].baseStateBytes=null;
 assert.equal(validateResources(v).levels[0].baseStateBytes,null);
 v.levels[1].baseStateBytes=2**60;assert.equal(validateResources(v).levels[1].baseStateBytes,2**60);
 v.levels[0].overflow=false;assert.throws(()=>validateResources(v));
});
test('field/AMR overlay requires all identities and authoritative matching EOS evidence',()=>{
 const core=example('sod-mesh.json');
 const identity={projectId:'p',caseId:'Sod',configRevision:core.identity.configRevision,buildId:'b',binarySha256:'sha',requestId:'mesh'};
 const mesh={identity,operation:'preview-amr',core} as WorkflowResult;
 const field={identity:{...identity,requestId:'field'},core:{data:{dimension:1},state:{eos:structuredClone(core.state.eos)}}} as unknown as RealPreviewResult;
 assert.equal(matchingAmrField(mesh,field),true);
 for(const key of ['projectId','caseId','configRevision','buildId','binarySha256'] as const){const f=structuredClone(field);f.identity[key]='different';assert.equal(matchingAmrField(mesh,f),false);}
 const m=structuredClone(mesh),f=structuredClone(field);
 Object.assign(m.core.state!.eos as object,{requested:'helmholtz',resolved:'helmholtz',sourceFingerprint:'old'});
 Object.assign(f.core.state!.eos as object,{requested:'helmholtz',resolved:'helmholtz',sourceFingerprint:'new'});
 assert.equal(matchingAmrField(m,f),false);
 Object.assign(f.core.state!.eos as object,{sourceFingerprint:'old'});assert.equal(matchingAmrField(m,f),true);
 Object.assign(m.core.state!.eos as object,{sourceFingerprint:null});Object.assign(f.core.state!.eos as object,{sourceFingerprint:null});assert.equal(matchingAmrField(m,f),false);
});


test('real hierarchy hit testing uses physical x/y and level visibility without changing geometry',()=>{
 const mesh=validateMesh(example('cellular-mesh.json').data,'ok');
 const before=JSON.stringify(mesh), levels=new Set(mesh.levelCounts.map(l=>l.level));
 for(const leaf of mesh.leaves){
  const x=(leaf.lower[0]+leaf.upper[0])/2,y=(leaf.lower[1]+leaf.upper[1])/2;
  assert.equal(amrAt(mesh,x,y,levels)?.logicalKey,leaf.logicalKey);
  assert.equal(amrAt(mesh,x,y,new Set([...levels].filter(l=>l!==leaf.level))),undefined);
 }
 assert.deepEqual(meshDomain(mesh,0),[0,25.6]);assert.deepEqual(meshDomain(mesh,1),[0,12.8]);
 assert.equal(amrAt(mesh,-1,-1,levels),undefined);assert.equal(JSON.stringify(mesh),before);
});
test('cell-line LOD resolves only visible native cells and never changes leaf counts',()=>{
 const mesh=validateMesh(example('sod-mesh.json').data,'ok'),leaf=mesh.leaves.find(l=>l.level===3)!;
 const before=JSON.stringify(mesh);
 assert.equal(visibleCellCoordinates(leaf,0,[0,1],100).length,0);
 const lines=visibleCellCoordinates(leaf,0,[leaf.lower[0],leaf.upper[0]],800);
 assert.equal(lines.length,leaf.cellShape[0]-1);
 for(const x of lines)assert.ok(x>leaf.lower[0]&&x<leaf.upper[0]);
 const mid=(leaf.lower[0]+leaf.upper[0])/2;
 assert.ok(visibleCellCoordinates(leaf,0,[mid,leaf.upper[0]],800).every(x=>x>=mid));
 assert.equal(JSON.stringify(mesh),before);
});
test('authoritative Cartesian reference has balanced 2:1 transitions',()=>{
 const mesh=validateMesh(example('cellular-mesh.json').data,'ok');
 let transitions=0;
 for(const a of mesh.leaves)for(const b of mesh.leaves)for(let axis=0;axis<2;axis++){
  const other=1-axis;
  if(Math.abs(a.upper[axis]-b.lower[axis])<1e-10&&Math.min(a.upper[other],b.upper[other])-Math.max(a.lower[other],b.lower[other])>1e-10){
   assert.ok(Math.abs(a.level-b.level)<=1);
   if(a.level!==b.level){transitions++;assert.ok(Math.abs(Math.max(a.cellSpacing[axis],b.cellSpacing[axis])/Math.min(a.cellSpacing[axis],b.cellSpacing[axis])-2)<1e-10);}
  }
 }
 assert.ok(transitions>0);
});
