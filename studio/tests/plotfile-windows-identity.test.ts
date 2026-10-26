import test from 'node:test';
import assert from 'node:assert/strict';
import {sourceEvidenceValid} from '../src/host/plotfileSourceIdentity.ts';

test('recorded main-image source admits exact Linux and Windows labels on either reader host',()=>{
 const evidence={version:'candidate-identity-1',scope:'partial',caseId:null,caseSource:null,
  rawConfigSha256:null,rawConfigSource:null,binarySha256:'b'.repeat(64),binarySource:'Windows mapped main executable',binaryScope:'main-executable-only',
  eosType:null,eosSource:null,eosTableState:'unknown',eosTableSha256:null,idealGamma:null,speciesState:'unknown',speciesNames:[],
  runId:null,effectiveConfigSha256:null,buildId:null,sourceGitHead:null,eosUnitSystem:null};
 assert.equal(sourceEvidenceValid(evidence),true);
 assert.equal(sourceEvidenceValid({...evidence,binarySource:'Linux /proc/self/exe'}),true);
 for(const change of [{binarySource:'Windows argv0'},{binarySource:'windows mapped main executable'},
  {binarySource:'unknown platform'},{binarySha256:'b'.repeat(63)},{binarySha256:null},
  {binaryScope:'all dependencies'},{binarySource:null},{binarySource:{toString:()=>evidence.binarySource}}])assert.equal(sourceEvidenceValid({...evidence,...change}),false);
 assert.equal(sourceEvidenceValid({...evidence,binarySha256:null,binarySource:null}),true);
});
