/** Candidate recorded evidence, never inferred from the currently selected project. */
export interface PlotfileSourceEvidence {
 version:'candidate-identity-1';scope:'partial';
 caseId:string|null;caseSource:'ConfigurationInput.case_id'|null;
 rawConfigSha256:string|null;rawConfigSource:'ConfigurationInput.raw_text; exact parser bytes'|null;
 binarySha256:string|null;binarySource:'Linux /proc/self/exe'|null;binaryScope:'main-executable-only';
 eosType:string|null;eosSource:'resolved-runtime-checkpoint-provenance'|null;
 eosTableState:'unknown'|'recorded'|'not-applicable';eosTableSha256:string|null;idealGamma:number|null;
 speciesState:'unknown'|'recorded';speciesNames:string[];
 runId:null;effectiveConfigSha256:null;buildId:null;sourceGitHead:null;eosUnitSystem:'cgs'|null;
}
const object=(v:unknown):v is Record<string,unknown>=>!!v&&typeof v==='object'&&!Array.isArray(v);
const text=(v:unknown)=>typeof v==='string'&&v.length>0&&v.length<=128&&!v.includes('\0');
const digest=(v:unknown)=>typeof v==='string'&&/^[a-f0-9]{64}$/.test(v);
export function sourceEvidenceValid(v:unknown):v is PlotfileSourceEvidence {
 if(!object(v)||v.version!=='candidate-identity-1'||v.scope!=='partial'||v.binaryScope!=='main-executable-only'||
  !['runId','effectiveConfigSha256','buildId','sourceGitHead'].every(k=>v[k]===null))return false;
 if(v.eosUnitSystem!==null&&v.eosUnitSystem!=='cgs')return false;
 if(v.caseId===null?v.caseSource!==null:!text(v.caseId)||v.caseSource!=='ConfigurationInput.case_id')return false;
 if(v.rawConfigSha256===null?v.rawConfigSource!==null:!digest(v.rawConfigSha256)||v.rawConfigSource!=='ConfigurationInput.raw_text; exact parser bytes')return false;
 if(v.binarySha256===null?v.binarySource!==null:!digest(v.binarySha256)||v.binarySource!=='Linux /proc/self/exe')return false;
 if(v.eosType===null?v.eosSource!==null:!text(v.eosType)||v.eosSource!=='resolved-runtime-checkpoint-provenance')return false;
 if(!['unknown','recorded','not-applicable'].includes(String(v.eosTableState))||
  (v.eosTableState==='recorded'?!digest(v.eosTableSha256)||v.eosType===null:v.eosTableSha256!==null))return false;
 if(v.eosType==='ideal'){
  if(v.eosTableState!=='not-applicable'||typeof v.idealGamma!=='number'||!Number.isFinite(v.idealGamma)||v.idealGamma<=1)return false;
 }else if(v.idealGamma!==null||v.eosTableState==='not-applicable')return false;
 if(!['unknown','recorded'].includes(String(v.speciesState))||!Array.isArray(v.speciesNames)||
  v.speciesNames.length>128||!v.speciesNames.every(text)||
  (v.speciesState==='unknown'?v.speciesNames.length!==0:v.eosType===null))return false;
 return true;
}
