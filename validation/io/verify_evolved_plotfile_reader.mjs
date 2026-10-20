/**
 * Independent h5py oracle -> production isolated reader -> client validation.
 * Arguments: local manifest JSON, processed summary JSON (must not already exist).
 * No UI/science acceptance claim; no input/output file mutation.
 */
import fs from 'node:fs/promises';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {inspectPlotfileMetadataIsolated,readPlotfileOverviewIsolated,readPlotfilePointIsolated} from '../../studio/host/isolatedPlotfileMetadata.ts';
import {validatePlotfileAudit,validatePlotfileOverview,validatePlotfilePoint} from '../../studio/src/host/plotfileAudit.ts';
import {PROTOCOL_VERSION} from '../../studio/src/host/contracts.ts';
import {sourceEvidenceValid} from '../../studio/src/host/plotfileSourceIdentity.ts';
/** Distinguish complete formal evidence from the explicitly supported historical candidate. */
function verifyIdentityVersion(audit,expected){
 const source=audit.candidateSourceIdentity;
 assert.ok(sourceEvidenceValid(source),'Require the production typed identity contract.');
 if(source.version==='arch-plot-identity-1'){
  assert.equal(source.scope,'resolved-runtime');assert.equal(source.recordsVerified,true);
  assert.equal(source.resolvedBackend,'cpu');
  assert.equal(audit.completion.state,'complete');
  assert.equal(audit.candidateNativeGrid.version,'arch-native-cartesian-1');
  assert.equal(audit.nativeCellGeometry?.bounds,'recorded');
  assert.equal(audit.nativeCellGeometry?.volume,'recorded');
 }else{
  assert.equal(source.version,'candidate-identity-1');assert.equal(source.scope,'partial');
  assert.equal(audit.candidateNativeGrid.version,'candidate-cartesian-1');
  assert.equal(audit.completion.state,'unknown');assert.equal(audit.renderEligible,false);
 }
 const matched=[];
 for(const key of ['caseSourceSha256','effectiveConfigSha256','buildId',
  'sourceManifestSha256','buildProfileSha256','eosType','eosIdentitySha256',
  'eosTableSha256','eosTableIdentityKind','idealGamma','speciesNames',
  'sourceGitHead','sourceGitDirty','sourceGitSource','resolvedBackend','eosUnitSystem']){
  if(Object.hasOwn(expected,key)){
   assert.deepEqual(source[key],expected[key],`Recorded evolved identity mismatch: ${key}`);
   matched.push(key);
  }
 }
 return matched;
}
assert.equal(process.argv.length,4,'Use local manifest and new summary path');
const oracle=JSON.parse(await fs.readFile(process.argv[2],'utf8'));
assert.ok(Array.isArray(oracle)&&oracle.length>0,'Require a nonempty evolved oracle.');
const bits=v=>{assert.equal(typeof v,'number');const b=Buffer.alloc(8);b.writeDoubleLE(v);return b.toString('hex');};
const sha=async p=>crypto.createHash('sha256').update(await fs.readFile(p)).digest('hex');
const rows=[];
for(const expected of oracle){
 const path=expected.path,identity={protocolVersion:PROTOCOL_VERSION,projectId:'evolved-readback',relativePath:path};
 assert.equal(await sha(path),expected.sha256);
 const metadata=await inspectPlotfileMetadataIsolated(path);
 const m=validatePlotfileAudit({...identity,metadata},identity.projectId,path).audit;
 assert.equal(m.file.sha256,expected.sha256);assert.equal(m.time,expected.time);
 assert.equal(m.dimension,1);assert.equal(m.geometry,'cartesian');
 assert.equal(m.candidateSourceIdentity.caseId,'Sod');
 assert.equal(m.candidateSourceIdentity.runId,expected.runId);
 assert.equal(m.candidateSourceIdentity.rawConfigSha256,expected.inputSha256);
 assert.equal(m.candidateSourceIdentity.binarySha256,expected.binarySha256);
 const matched=verifyIdentityVersion(m,expected);
 assert.equal(m.candidateNativeGrid.measureUnit,'cm');
 assert.equal(m.candidateNativeGrid.measureNormalization,'per_unit_transverse_area');
 const request={field:'DENS',width:32,height:1};
 const overview=await readPlotfileOverviewIsolated(path,request);
 validatePlotfileOverview({...identity,result:overview},identity.projectId,path,request,expected.sha256);
 assert.deepEqual(overview.candidateSourceIdentity,m.candidateSourceIdentity);
 assert.equal(overview.overview.scannedCells,expected.cells);
 let compared=0;
 for(const sample of expected.points)for(const [field,value] of Object.entries(sample.fields)){
  const query={field,point:sample.point};
  const result=await readPlotfilePointIsolated(path,query);
  const a=validatePlotfilePoint({...identity,result},identity.projectId,path,query,expected.sha256).audit;
  assert.deepEqual(a.candidateSourceIdentity,m.candidateSourceIdentity);
  const p=a.payload,n=p.nativeCells;
  assert.equal(p.block,sample.block);assert.deepEqual(p.start,sample.start);
  assert.deepEqual(p.linearIndices,[sample.index]);assert.equal(p.unit,value.unit);
  assert.equal(bits(p.values[0]),value.bits);
  assert.equal(n.logicalKey,sample.logicalKey);assert.equal(n.level,sample.level);
  assert.equal(bits(n.cellMeasure[0]),sample.measure);
  for(const axis of ['x1','x2','x3']){
   assert.equal(bits(n.lower[axis][0]),sample.lower[axis]);
   assert.equal(bits(n.upper[axis][0]),sample.upper[axis]);
  }
  for(const axis of ['x','y','z'])assert.equal(bits(p.coordinates[axis][0]),sample.coordinates[axis]);
  compared++;
 }
 assert.equal(await sha(path),expected.sha256);
 rows.push({mode:expected.mode,time:expected.time,fileSha256:expected.sha256,runId:expected.runId,
  inputSha256:expected.inputSha256,binarySha256:expected.binarySha256,
  nativeFieldPoints:compared,overviewScannedCells:overview.overview.scannedCells,
  metadataPointOverviewClient:'PASS',nativeFp64ValuesBoundsMeasureCoordinates:'bit-identical',
  fileUnchanged:true,completion:m.completion.state,renderEligible:m.renderEligible,
  identityVersion:m.candidateSourceIdentity.version,scientificIdentity:m.scientificIdentity,
  matchedOptionalRunIdentityFields:matched});
}
const summary={status:'PASS',scope:'Existing evolved Sod files; production isolated reader and client validators, independent h5py stored-cell oracle. No HTTP/native UI/science certification.',
 files:rows.length,fieldPoints:rows.reduce((n,r)=>n+r.nativeFieldPoints,0),rows,
 limitations:['No independent physics oracle','No Cartesian 2D evolution','No full-domain point enumeration','No native desktop UAT','Whole-file digest and leaf scan remain']};
await fs.writeFile(process.argv[3],JSON.stringify(summary,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify(summary,null,2));
