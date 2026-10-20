/** Verify every exported field through the production isolated native-point reader. */
import {readFileSync,readdirSync} from 'node:fs';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import {readPlotfilePointIsolated} from '../../studio/host/isolatedPlotfileMetadata.ts';
import {validatePlotfilePoint} from '../../studio/src/host/plotfileAudit.ts';
const [runFile,readerFile]=process.argv.slice(2);assert.equal(process.argv.length,4);
const runs=JSON.parse(readFileSync(runFile,'utf8'));
const expected=JSON.parse(readFileSync(readerFile,'utf8'));
const rows=[];
for(const run of runs){
 const directory=join(run.localEvidenceDirectory,'output');
 const files=readdirSync(directory).filter(n=>n.includes('_plt_')&&n.endsWith('.h5'));
 assert.equal(files.length,1);
 const path=join(directory,files[0]),sha=()=>createHash('sha256').update(readFileSync(path)).digest('hex');
 const before=sha(),reference=expected.find(r=>r.case===run.case);assert.equal(before,reference.fileSha256);
 const point=reference.dimension===1?[.49]:[.5,6.5],fields=[];
 for(const field of reference.exportedFields){
  const request={field:field.field,point};
  const result=await readPlotfilePointIsolated(path,request);
  validatePlotfilePoint({protocolVersion:'1.3',projectId:'isolated-all-fields',relativePath:path,result},
   'isolated-all-fields',path,request,before);
  assert.equal(result.candidateSourceIdentity.caseId,run.case);
  assert.equal(result.candidateSourceIdentity.binarySha256,run.binarySha256);
  assert.equal(result.payload.linearIndices[0],field.pointIndex);
  assert(Object.is(result.payload.values[0],field.rawPointValue));
  fields.push({field:field.field,index:field.pointIndex,rawValue:field.rawPointValue,bitExactNumericPoint:true});
 }
 assert.equal(sha(),before);
 rows.push({case:run.case,fileSha256:before,fieldCount:fields.length,fields,sourceUnchanged:true});
}
console.log(JSON.stringify({rows,scope:'Production isolated reader/client raw points; not full-array query or independent science acceptance'},null,2));
