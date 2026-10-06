/** Production isolated reader/client verification of locally generated t=0 run evidence. */
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import {readPlotfilePointIsolated} from '../../studio/host/isolatedPlotfileMetadata.ts';
import {validatePlotfilePoint} from '../../studio/src/host/plotfileAudit.ts';
const runs=JSON.parse(readFileSync(process.argv[2],'utf8'));
const results=[];
for(const run of runs){
 const sha=()=>createHash('sha256').update(readFileSync(run.plotfile)).digest('hex');
 assert.equal(sha(),run.plotfileSha256);
 const request={field:'DENS',point:run.case==='Sod'?[.49]:[.5,6.5]};
 const result=await readPlotfilePointIsolated(run.plotfile,request);
 validatePlotfilePoint({protocolVersion:'1.3',projectId:'run-evidence',relativePath:run.plotfile,result},
  'run-evidence',run.plotfile,request,run.plotfileSha256);
 assert.equal(result.candidateSourceIdentity.runId,run.runId);
 assert.equal(result.candidateSourceIdentity.runIdSource,'DriverIO output session; OS-generated UUIDv4');
 assert.equal(result.candidateSourceIdentity.binarySha256,run.binarySha256);
 assert.equal(result.candidateSourceIdentity.rawConfigSha256,run.inputSha256);
 for(const key of ['effectiveConfigSha256','buildId','sourceGitHead'])
  assert.equal(result.candidateSourceIdentity[key],null);
 assert.equal(sha(),run.plotfileSha256);
 results.push({case:run.case,runId:run.runId,productionIsolatedReaderAndClient:true,sourceUnchanged:true});
}
console.log(JSON.stringify(results,null,2));
