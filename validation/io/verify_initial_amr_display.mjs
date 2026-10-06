/** Actual response -> runtime validator -> native slice/Inspector math. No UI claim. */
import fs from 'node:fs';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import {validateWorkflowCore} from '../../studio/src/host/workflowValidation.ts';
import {amrAt,amrPlaneAxes,amrPlaneLeaves,meshDomain} from '../../studio/src/data/amrGeometry.ts';
const [responsePath,inputPath,caseId,requestId]=process.argv.slice(2);
assert.equal(process.argv.length,6);
const raw=fs.readFileSync(inputPath);
const core=validateWorkflowCore(JSON.parse(fs.readFileSync(responsePath,'utf8')),'preview-amr',{
 caseId,requestId,configRevision:createHash('sha256').update(raw).digest('hex')
});
const mesh=core.data;assert.equal(mesh.dimension,3);
const before=JSON.stringify(mesh),levels=new Set(mesh.leaves.map(l=>l.level));
let centerHits=0,planeMemberships=0;
for(let axis=0;axis<3;axis++){
 const domain=meshDomain(mesh,axis);
 for(const coordinate of new Set([domain[0],domain[1],...mesh.leaves.flatMap(l=>[l.lower[axis],l.upper[axis],(l.lower[axis]+l.upper[axis])/2])])){
  const slice={axis,coordinate},axes=amrPlaneAxes(mesh,slice);
  const independent=mesh.leaves.filter(l=>coordinate>=l.lower[axis]&&(coordinate<l.upper[axis]||coordinate===domain[1]&&l.upper[axis]===domain[1]));
  const projected=amrPlaneLeaves(mesh,slice);assert.deepEqual(projected,independent);
  planeMemberships+=projected.length;
  for(const leaf of projected){
   const [x,y]=axes.map(a=>(leaf.lower[a]+leaf.upper[a])/2);
   assert.equal(amrAt(mesh,x,y,levels,slice)?.logicalKey,leaf.logicalKey);centerHits++;
   assert.equal(amrAt(mesh,x,y,new Set(),slice),undefined);
  }
 }
}
assert.equal(JSON.stringify(mesh),before);
console.log(JSON.stringify({version:'initial-amr-display-response-1',caseId,dimension:mesh.dimension,
 status:core.status,complete:mesh.complete,leafCount:mesh.leafCount,levelCounts:mesh.levelCounts,
 centerHits,planeMemberships,axes:3,rawMeshUnchanged:true,
 scope:'Runtime validator and all native slice/center hit mappings; no canvas or desktop UAT, no AMR cell field array'},null,2));
