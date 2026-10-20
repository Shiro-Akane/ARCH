/** Candidate recorded evidence, never inferred from the currently selected project. */
export type PlotfileSpeciesProperties={
 version:'checkpoint-species-1';state:'recorded';source:'resolved-runtime-checkpoint-provenance';
 values:{A:number[];Z:number[];gamma:number[];Cv:number[]};
}|{version:'checkpoint-species-1';state:'unknown';source:null;values:null;reason:string};
export interface PlotfileSourceEvidence {
 version:'candidate-identity-1'|'arch-plot-identity-1';scope:'partial'|'resolved-runtime';
 caseId:string|null;caseSource:'ConfigurationInput.case_id'|null;
 rawConfigSha256:string|null;rawConfigSource:'ConfigurationInput.raw_text; exact parser bytes'|null;
 binarySha256:string|null;binarySource:'Linux /proc/self/exe'|null;binaryScope:'main-executable-only';
 eosType:string|null;eosSource:'resolved-runtime-checkpoint-provenance'|null;
 eosTableState:'unknown'|'recorded'|'not-applicable';eosTableSha256:string|null;idealGamma:number|null;
 speciesState:'unknown'|'recorded';speciesNames:string[];speciesProperties?:PlotfileSpeciesProperties;
 unknownIdentityReasons?:{effectiveConfigSha256:string;buildId:string;sourceGitHead:string};
 runId:string|null;runIdSource?:string|null;effectiveConfigSha256:string|null;buildId:string|null;sourceGitHead:string|null;eosUnitSystem:'cgs'|null;
 caseSourceSha256?:string;effectiveConfigVersion?:'arch-effective-configuration-1';effectiveConfigSource?:string;
 buildIdentityVersion?:'arch-build-identity-1';buildIdentityScope?:'project-source-and-configured-compiler-profile';
 buildSource?:string;sourceManifestSha256?:string;buildProfileSha256?:string;
 sourceGitDirty?:'true'|'false'|'unknown';sourceGitSource?:'optional-build-time-annotation';
 eosIdentityVersion?:'arch-eos-identity-1';eosIdentitySha256?:string;eosTableIdentityKind?:string;
 resolvedBackend?:'cpu'|'cuda';recordsVerified?:true;

}
const object=(v:unknown):v is Record<string,unknown>=>!!v&&typeof v==='object'&&!Array.isArray(v);
const text=(v:unknown)=>typeof v==='string'&&v.length>0&&v.length<=128&&!v.includes('\0');
const digest=(v:unknown)=>typeof v==='string'&&/^[a-f0-9]{64}$/.test(v);
export function sourceEvidenceValid(v:unknown):v is PlotfileSourceEvidence {
 if(!object(v)||v.binaryScope!=='main-executable-only')return false;
 const formal=v.version==='arch-plot-identity-1';
 if(formal){
  if(v.scope!=='resolved-runtime'||v.effectiveConfigVersion!=='arch-effective-configuration-1'||
   v.effectiveConfigSource!=='immutable-runtime-config-and-resolved-plan'||
   v.buildIdentityVersion!=='arch-build-identity-1'||v.buildIdentityScope!=='project-source-and-configured-compiler-profile'||
   v.buildSource!=='CMake-embedded-project-source-and-compiler-profile'||
   ![v.caseSourceSha256,v.effectiveConfigSha256,v.buildId,v.sourceManifestSha256,v.buildProfileSha256,v.eosIdentitySha256].every(digest)||
   v.eosIdentityVersion!=='arch-eos-identity-1'||v.recordsVerified!==true||
   !['true','false','unknown'].includes(String(v.sourceGitDirty))||v.sourceGitSource!=='optional-build-time-annotation'||
   v.sourceGitHead!==null&&(typeof v.sourceGitHead!=='string'||!/^[a-f0-9]{40}$/.test(v.sourceGitHead))||
   !['cpu','cuda'].includes(String(v.resolvedBackend))||v.unknownIdentityReasons!==undefined||
   v.caseId===null||v.runId===null||v.binarySha256===null||v.rawConfigSha256===null||v.eosType===null||v.eosUnitSystem!=='cgs')return false;
  const kind=v.eosType==='ideal'?'not-applicable':v.eosType==='helmholtz'?'accepted-table-content':'accepted-table-content-and-component-interpretation';
  if(v.eosTableIdentityKind!==kind||v.eosType!=='ideal'&&v.eosTableState!=='recorded'||
    !object(v.speciesProperties)||v.speciesProperties.state!=='recorded')return false;
 }else if(v.version!=='candidate-identity-1'||v.scope!=='partial'||
  !['effectiveConfigSha256','buildId','sourceGitHead'].every(k=>v[k]===null))return false;
 if(v.unknownIdentityReasons!==undefined){
  const reasons=v.unknownIdentityReasons;
  if(!object(reasons)||Object.keys(reasons).sort().join(',')!=='buildId,effectiveConfigSha256,sourceGitHead'||
   !Object.values(reasons).every(reason=>typeof reason==='string'&&reason.trim().length>0&&
    reason.length<=256&&!reason.includes('\0')))return false;
 }
 if(v.runId===null ? v.runIdSource!==undefined&&v.runIdSource!==null :
  typeof v.runId!=='string'||!/^[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}$/.test(v.runId)||
  v.runIdSource!=='DriverIO output session; OS-generated UUIDv4')return false;
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
 if(v.speciesProperties!==undefined){
  const p=v.speciesProperties;
  if(!object(p)||p.version!=='checkpoint-species-1')return false;
  if(p.state==='unknown'){
   if(p.source!==null||p.values!==null||typeof p.reason!=='string'||!p.reason.length||p.reason.length>256||p.reason.includes('\0'))return false;
  }else if(p.state==='recorded'){
   if(v.speciesState!=='recorded'||!v.speciesNames.length&&(!formal||v.eosType!=='ideal')||
    p.source!=='resolved-runtime-checkpoint-provenance'||!object(p.values)||
    Object.keys(p.values).sort().join(',')!=='A,Cv,Z,gamma')return false;
   for(const key of ['A','Z','gamma','Cv']){
    const values=p.values[key];
    if(!Array.isArray(values)||values.length!==v.speciesNames.length||
      !values.every(x=>typeof x==='number'&&Number.isFinite(x)))return false;
   }
  }else return false;
 }
 return true;
}
