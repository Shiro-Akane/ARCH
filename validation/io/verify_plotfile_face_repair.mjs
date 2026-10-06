/** Exact independent point expectations for repaired real Plotfile faces. */
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import {readPlotfilePointIsolated} from '../../studio/host/isolatedPlotfileMetadata.ts';
import {validatePlotfilePoint} from '../../studio/src/host/plotfileAudit.ts';
const [input]=process.argv.slice(2);assert.equal(process.argv.length,3);
const {plot,expectedSha256,queries}=JSON.parse(readFileSync(input,'utf8'));
const hash=()=>createHash('sha256').update(readFileSync(plot)).digest('hex');
assert.equal(hash(),expectedSha256);
const results=[];
for(const q of queries){
 const query={field:'DENS',point:q.point};
 const result=await readPlotfilePointIsolated(plot,query);
 validatePlotfilePoint({protocolVersion:'1.3',projectId:'face-repair',relativePath:plot,result},
  'face-repair',plot,query,expectedSha256);
 assert.equal(result.pointEvidence.matchCount,1);
 assert.equal(result.payload.linearIndices[0],q.index);
 assert.equal(result.payload.values[0],q.value);
 results.push({...q,matchCount:1,scannedCells:result.pointEvidence.scannedCells,clientValidation:'PASS'});
}
assert.equal(hash(),expectedSha256);
console.log(JSON.stringify({plotfileSha256:expectedSha256,sourceUnchanged:true,results,
 scope:'Real Cartesian t=0 native point repair only; no independent EOS or evolution acceptance'},null,2));
