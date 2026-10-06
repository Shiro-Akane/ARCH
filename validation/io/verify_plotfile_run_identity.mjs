/**
 * Verify approved CPU t=0 run evidence through the production isolated reader/client.
 * Require formal publication and verified runtime records, then match identities
 * actually supplied by the run manifest. This does not accept a numerical solution.
 */
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import {readPlotfilePointIsolated} from '../../studio/host/isolatedPlotfileMetadata.ts';
import {validatePlotfilePoint} from '../../studio/src/host/plotfileAudit.ts';
import {sourceEvidenceValid} from '../../studio/src/host/plotfileSourceIdentity.ts';

/** Require the existing formal contract; compare optional reference fields only when recorded. */
function verifyFormalIdentity(audit,run){
 const identity=audit.candidateSourceIdentity;
 assert.ok(sourceEvidenceValid(identity),'Runtime identity must satisfy the production typed contract.');
 assert.equal(identity.version,'arch-plot-identity-1');
 assert.equal(identity.scope,'resolved-runtime');
 assert.equal(identity.recordsVerified,true);
 assert.equal(audit.completion.state,'complete');
 assert.equal(audit.candidateNativeGrid?.version,'arch-native-cartesian-1');
 assert.equal(audit.nativeCellGeometry?.bounds,'recorded');
 assert.equal(audit.nativeCellGeometry?.volume,'recorded');
 assert.equal(audit.geometry,'cartesian');
 assert.equal(audit.time,0);
 assert.equal(identity.resolvedBackend,'cpu');
 assert.equal(identity.caseId,run.case);
 assert.deepEqual(audit.scientificIdentity,{case:identity.caseId,
  config:identity.effectiveConfigSha256,build:identity.buildId,
  binary:identity.binarySha256,eos:identity.eosIdentitySha256});
 // The formal reader maps only the recorded "not-applicable" Git annotation
 // to null; configuration, build and EOS identities remain required digests.
 assert.equal(identity.sourceGitSource,'optional-build-time-annotation');
 const matched=[];
 for(const key of ['caseSourceSha256','effectiveConfigSha256','buildId',
  'sourceManifestSha256','buildProfileSha256','eosType','eosIdentitySha256',
  'eosTableSha256','eosTableIdentityKind','idealGamma','speciesNames',
  'sourceGitHead','sourceGitDirty','sourceGitSource','resolvedBackend','eosUnitSystem']){
  if(Object.hasOwn(run,key)){
   assert.deepEqual(identity[key],run[key],`Recorded run identity mismatch: ${key}`);
   matched.push(key);
  }
 }
 return {identity,matched};
}

const runs=JSON.parse(readFileSync(process.argv[2],'utf8'));
assert.ok(Array.isArray(runs)&&runs.length>0,'Require a nonempty approved run manifest.');
const results=[];
for(const run of runs){
 assert.ok(run&&typeof run==='object'&&['Sod','CellularDet'].includes(run.case),
  'This verifier accepts only approved Sod/CellularDet t=0 records.');
 const sha=()=>createHash('sha256').update(readFileSync(run.plotfile)).digest('hex');
 assert.equal(sha(),run.plotfileSha256);
 const request={field:'DENS',point:run.case==='Sod'?[.49]:[.5,6.5]};
 const result=await readPlotfilePointIsolated(run.plotfile,request);
 const {audit}=validatePlotfilePoint({protocolVersion:'1.3',projectId:'run-evidence',relativePath:run.plotfile,result},
  'run-evidence',run.plotfile,request,run.plotfileSha256);
 const {identity,matched}=verifyFormalIdentity(audit,run);
 assert.equal(identity.runId,run.runId);
 assert.equal(identity.runIdSource,'DriverIO output session; OS-generated UUIDv4');
 assert.equal(identity.binarySha256,run.binarySha256);
 assert.equal(identity.rawConfigSha256,run.inputSha256);
 assert.equal(sha(),run.plotfileSha256);
 results.push({case:run.case,runId:run.runId,productionIsolatedReaderAndClient:true,sourceUnchanged:true,
  formalPublicationAndRuntimeRecordsVerified:true,scientificIdentity:audit.scientificIdentity,
  caseSourceSha256:identity.caseSourceSha256,sourceManifestSha256:identity.sourceManifestSha256,
  buildProfileSha256:identity.buildProfileSha256,sourceGitHead:identity.sourceGitHead,
  sourceGitSource:identity.sourceGitSource,matchedOptionalRunIdentityFields:matched,
  scope:'formal writer/reader identity only; no numerical acceptance'});
}
console.log(JSON.stringify(results,null,2));
