import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,copyFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import h5 from 'h5wasm/node';
import {sourceEvidenceValid} from '../src/host/plotfileSourceIdentity.ts';
import {inspectPlotfileMetadata} from '../host/plotfileMetadata.ts';
import {validatePlotfileAudit} from '../src/host/plotfileAudit.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
const valid={version:'candidate-identity-1',scope:'partial',caseId:'Sod',caseSource:'ConfigurationInput.case_id',
 rawConfigSha256:'a'.repeat(64),rawConfigSource:'ConfigurationInput.raw_text; exact parser bytes',
 binarySha256:'b'.repeat(64),binarySource:'Linux /proc/self/exe',binaryScope:'main-executable-only',
 eosType:'ideal',eosSource:'resolved-runtime-checkpoint-provenance',eosTableState:'not-applicable',
 eosTableSha256:null,idealGamma:1.4,speciesState:'recorded',speciesNames:['first','second'],
 runId:null,effectiveConfigSha256:null,buildId:null,sourceGitHead:null,eosUnitSystem:null};
test('source evidence separates partial recorded values from unsupported full/build/unit claims',()=>{
 assert.ok(sourceEvidenceValid(valid));
 assert.ok(sourceEvidenceValid({...valid,eosUnitSystem:'cgs'}));
 for(const change of [{scope:'complete'},{version:'future'},{buildId:'guessed'},{eosUnitSystem:'CGS'},
  {rawConfigSource:'current path'},{binarySource:'argv0'},{binaryScope:'all dependencies'},
  {caseId:null},{rawConfigSha256:'wrong'},{binarySha256:'B'.repeat(64)},
  {idealGamma:NaN},{idealGamma:1},{eosTableState:'recorded',eosTableSha256:'c'.repeat(64)},
  {speciesState:'unknown'},{speciesNames:new Array(129).fill('x')},{speciesNames:['']},
 ])assert.equal(sourceEvidenceValid({...valid,...change}),false);
 assert.ok(sourceEvidenceValid({...valid,eosType:'tabular3d',idealGamma:null,eosTableState:'recorded',eosTableSha256:'c'.repeat(64)}));
});
await h5.ready;
async function fixture(run:(path:string)=>Promise<void>,changes:Record<string,string|number>={},properties?:(group:InstanceType<typeof h5.Group>)=>void){
 const dir=await mkdtemp(join(tmpdir(),'arch-source-evidence-')),path=join(dir,'fixture.h5');
 try{
  await copyFile(new URL('./fixtures/sod-1d.h5',import.meta.url),path);
  const f=new h5.File(path,'a');
  try{
   f.create_attribute('plot_identity_state','unknown');
   const g=f.create_group('SourceIdentity');
   const attrs={version:'candidate-identity-1',scope:'partial',case_id:'Sod',case_source:'ConfigurationInput.case_id',
    raw_config_sha256:'a'.repeat(64),raw_config_source:'ConfigurationInput.raw_text; exact parser bytes',
    binary_sha256:'b'.repeat(64),binary_source:'Linux /proc/self/exe',binary_scope:'main-executable-only',
    eos_type:'ideal',eos_source:'resolved-runtime-checkpoint-provenance',eos_table_state:'not-applicable',
    eos_table_sha256:'unknown',ideal_gamma_available:1,ideal_gamma:1.4,
    species_identity_state:'recorded',species_count:2,run_id:'unknown',effective_config_sha256:'unknown',
    build_id:'unknown',source_git_head:'unknown',eos_unit_system:'unknown',...changes};
   for(const [k,v] of Object.entries(attrs))g.create_attribute(k,v);
   g.create_dataset({name:'species_names',data:['first','second']});
   properties?.(g);
  }finally{f.close();}
  await run(path);
 }finally{await rm(dir,{recursive:true,force:true});}
}
test('reader -> client preserves source evidence and unknowns without certifying completion',async()=>{
 await fixture(async path=>{
  const metadata=await inspectPlotfileMetadata(path);
  assert.deepEqual(metadata.candidateSourceIdentity,valid);
  const response={protocolVersion:PROTOCOL_VERSION,projectId:'session',relativePath:'candidate.h5',metadata};
  assert.equal(validatePlotfileAudit(response,'session','candidate.h5').audit.renderEligible,false);
  assert.equal(metadata.completion.state,'unknown');
  assert.throws(()=>validatePlotfileAudit({...response,metadata:{...metadata,candidateSourceIdentity:{...valid,buildId:'fabricated'}}},'session','candidate.h5'),/source evidence/);
 });
});
test('unknown candidate version, bad digest, EOS contradiction and species budget reject at reader',async()=>{
 for(const changes of [{version:'future'},{raw_config_sha256:'bad'},{build_id:'guessed'},
  {eos_table_state:'recorded',eos_table_sha256:'c'.repeat(64)},{species_count:129}])
  await fixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/candidate|budget/i);},changes);
 const legacy=await inspectPlotfileMetadata(new URL('./fixtures/sod-1d.h5',import.meta.url).pathname);
 assert.equal(legacy.candidateSourceIdentity,null);
});

const properties={version:'checkpoint-species-1',state:'recorded',
 source:'resolved-runtime-checkpoint-provenance',values:{A:[12,16],Z:[6,8],gamma:[1.4,5/3],Cv:[3,4]}};
const propertyAttrs={species_properties_version:'checkpoint-species-1',
 species_properties_state:'recorded',species_properties_source:'resolved-runtime-checkpoint-provenance'};
function writeProperties(group:InstanceType<typeof h5.Group>,replace?:{key:string;data:Float64Array|Float32Array}){
 for(const [key,values] of Object.entries(properties.values))
  group.create_dataset({name:'species_'+key,data:replace?.key===key?replace.data:new Float64Array(values)});
}
test('bounded EOS constituents preserve runtime order and raw FP64 through reader/client',async()=>{
 await fixture(async path=>{
  const metadata=await inspectPlotfileMetadata(path);
  assert.deepEqual(metadata.candidateSourceIdentity?.speciesProperties,properties);
  const response={protocolVersion:PROTOCOL_VERSION,projectId:'session',relativePath:'candidate.h5',metadata};
  const result=validatePlotfileAudit(response,'session','candidate.h5').audit;
  assert.deepEqual(result.candidateSourceIdentity?.speciesProperties,properties);
  assert.equal(result.completion.state,'unknown');
  assert.equal(result.renderEligible,false);
 },propertyAttrs,group=>writeProperties(group));
});
test('optional EOS constituents reject forged source, unbounded/partial/nonfinite vectors',()=>{
 assert.ok(sourceEvidenceValid({...valid,speciesProperties:properties}));
 const bad=[{version:'future'},{source:'schema defaults'},{state:'complete'},
  {values:{...properties.values,A:[12]}},{values:{...properties.values,Cv:[3,NaN]}},
  {values:{...properties.values,Z:new Array(129).fill(1)}},
  {values:{...properties.values,extra:[]}}];
 for(const change of bad)assert.equal(sourceEvidenceValid({...valid,speciesProperties:{...properties,...change}}),false);
 assert.equal(sourceEvidenceValid({...valid,speciesState:'unknown',speciesNames:[],speciesProperties:properties}),false);
});
test('reader rejects EOS property float32/shape/NaN/version/unknown contradictions',async()=>{
 for(const replace of [{key:'Cv',data:new Float32Array([3,4])},
  {key:'A',data:new Float64Array([12])},{key:'Z',data:new Float64Array([6,NaN])}])
  await fixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/species properties/i);},
   propertyAttrs,group=>writeProperties(group,replace));
 await fixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/species properties/i);},
  {...propertyAttrs,species_properties_version:'future'},group=>writeProperties(group));
 await fixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/species properties/i);},
  {},group=>writeProperties(group));
 await fixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/species properties/i);},
  {...propertyAttrs,species_properties_state:'unknown',species_properties_source:'unknown',
   species_properties_reason:'not supplied'},group=>writeProperties(group));
});
test('explicit unknown EOS properties preserve reason without synthetic values',async()=>{
 const unknown={version:'checkpoint-species-1',state:'unknown',source:null,values:null,
  reason:'resolved species properties not supplied by caller'};
 assert.ok(sourceEvidenceValid({...valid,speciesProperties:unknown}));
 assert.equal(sourceEvidenceValid({...valid,speciesProperties:{...unknown,reason:''}}),false);
 await fixture(async path=>{
  const metadata=await inspectPlotfileMetadata(path);
  assert.deepEqual(metadata.candidateSourceIdentity?.speciesProperties,unknown);
 },{species_properties_version:'checkpoint-species-1',species_properties_state:'unknown',
  species_properties_source:'unknown',species_properties_reason:unknown.reason});
});
