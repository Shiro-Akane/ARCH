/** Explicit real-binary integration; no Build, Setup, Preview or simulation. */
import assert from 'node:assert/strict';
import {copyFile,readFile,writeFile,readdir,mkdtemp,rm} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve,join} from 'node:path';
import {tmpdir} from 'node:os';
import {ConfigurationAdapter} from '../host/configuration.ts';
import {validateConfigurationSchema} from '../src/host/configurationValidation.ts';
import {catalog} from '../src/data/parameterCatalog.ts';
import {loadPar,editPar,exportDraft,parErrors} from '../src/state/parState.ts';
import {effectiveEntries} from '../src/data/ParDocument.ts';
import {EditHistory} from '../src/state/editHistory.ts';

const binary=process.argv[2];
if(!binary)throw new Error('Pass an explicitly built CPU ARCH binary.');
const root=await mkdtemp(join(tmpdir(),'arch-config-v3-'));
const f={root,cleanup:()=>rm(root,{recursive:true,force:true})};
try{
 await copyFile(resolve(binary),f.root+'/ARCH');
 // Exact copied executable identity only; never manufacture a successful Core
 // Build/source association for this bounded static inspection fixture.
 const adapter=new ConfigurationAdapter({root:f.root,projectId:'p',binaryRelativePath:'ARCH'});
 const schema=await adapter.schema();assert.equal(schema.core.version,'3');
 assert.equal(schema.buildId,'selected-binary:'+schema.binarySha256);
 const currentSchema=validateConfigurationSchema(schema.core);
 const keys=new Set(currentSchema.parameters.map(p=>p.key));
 assert.equal(keys.size,currentSchema.parameters.length);
 const jeans=currentSchema.parameters.find(p=>p.key==='jeans_cells')!;
 assert.ok(jeans);assert.equal(jeans.allowedDefault,null);
 assert.equal(jeans.presentation?.subgroup,'AMR');
 assert.equal(jeans.units.unit,'1');assert.equal(jeans.constraints.min,4);
 assert.equal(jeans.constraints.minInclusive,true);
 const text=await readFile(new URL('../../src/api/examples/configuration-v3/sod-valid.par',import.meta.url),'utf8');
 await writeFile(f.root+'/untouched.par','disk original');
 const before=await readdir(f.root);
 for(const [input,status,kind] of [[text,'ok','complete'],['','error','incomplete'],
  [text+'\nx_pos=0.25\nbad line\n','error','invalid'],
  [text+Array.from({length:30000},(_,i)=>'unknown_key_'+i+'=0\n').join(''),'error','undetermined']]){
  const result=await adapter.inspect({projectId:'p',caseId:'Sod',configText:input,
   configRevision:createHash('sha256').update(input).digest('hex')});
  assert.equal(result.core.status,status);
  assert.equal(result.core.completeness.state,kind);
  assert.equal(result.identity.binarySha256,schema.binarySha256);
  assert.equal(result.core.identity.configRevision,result.identity.configRevision);
  assert.equal(result.core.execution.setup,'not_executed');
  if(kind==='complete'){
   assert.equal(result.core.coordinates?.dimension,1);
   assert.equal(result.core.diffusion?.enabled,false);
   assert.equal(result.core.diffusion?.source,'constant');
   assert.equal(result.core.amrIndicators?.choices.find(c=>c.value==='DENS')?.selected,true);
   assert.equal(result.core.coordinates?.axes[0].unit,'cm');
  }
  if(kind==='incomplete'){
   assert.equal(result.core.coordinates,undefined);
   assert.equal(result.core.diffusion,undefined);
   assert.equal(result.core.amrIndicators,undefined);
   const value=result.core.parameters.find(p=>p.key==='cfl')!;
   assert.equal(value.parsedValue,null);assert.equal(value.resolvedValue,null);
  }
  if(kind==='undetermined'){
   assert.ok(result.core.diagnostics.some(d=>d.code==='RESPONSE_TOO_LARGE'));
   assert.equal(result.core.coverage.diagnosticsComplete,false);
  }
 }
 // Exercise actual Core condition/availability through the production Host
 // validator plus the same catalog/Working Copy/Undo owners used by Studio.
 // Sod here is static input inspection only, not a self-gravity science case.
 const configure=(input:string,changes:Record<string,string>)=>{
  let state=loadPar('unsaved.par',input);
  for(const [key,value] of Object.entries(changes))state=editPar(state,key,value);
  return exportDraft(state).text;
 };
 const inspectText=(configText:string)=>adapter.inspect({projectId:'p',caseId:'Sod',configText,
  configRevision:createHash('sha256').update(configText).digest('hex')});
 const selfText=configure(text,{gravity_type:'self',gravity_boundary:'periodic',
  gravity_rtol:'1e-10',gravity_atol:'0',gravity_max_cycles:'100',compute_backend:'cpu',
  x1l_boundary_type:'periodic',x1r_boundary_type:'periodic',lrefinemax:'1',max_blocks:'64'});
 const unselected=await inspectText(selfText);
 const choice=unselected.core.amrIndicators?.choices.find(c=>c.value==='JENS');
 assert.equal(unselected.core.status,'ok');assert.equal(choice?.available,true);
 assert.equal(choice?.selected,false);assert.equal(choice?.reason,null);
 const missingText=configure(selfText,{refine_var:'JENS'});
 const missing=await inspectText(missingText);
 assert.equal(missing.core.status,'error');
 const missingTarget=missing.core.parameters.find(p=>p.key==='jeans_cells')!;
 assert.equal(missingTarget.inputState,'missing');assert.equal(missingTarget.parsedValue,null);
 assert.equal(missingTarget.valueSource,null);assert.equal(missingTarget.requirement.required,true);
 assert.ok(missing.core.diagnostics.some(d=>d.parameterKey==='jeans_cells'&&d.severity==='error'));
 const working=loadPar('unsaved.par',missingText);
 const workingValues=Object.fromEntries(effectiveEntries(working.document).map(e=>[e.key,e.value]));
 const row=catalog(currentSchema.parameters,workingValues).find(r=>r.parameter.key==='jeans_cells')!;
 assert.equal(row.explicit,false);assert.equal(row.value,'');assert.equal(row.sourceKey,'jeans_cells');
 assert.equal(exportDraft(working).text,missingText);
 const edited=editPar(working,'jeans_cells','160');
 const editedText=exportDraft(edited).text;
 assert.equal(editedText.match(/^jeans_cells\s*=/gm)?.length,1);
 assert.equal(Object.keys(edited.changes).length,1);
 const history=new EditHistory<typeof working>();history.record(working,edited);
 assert.equal(exportDraft(history.undo(edited)).text,missingText);
 assert.equal(exportDraft(history.redo(working)).text,editedText);
 const active=await inspectText(editedText);
 assert.equal(active.core.status,'ok');
 assert.equal(active.core.amrIndicators?.choices.find(c=>c.value==='JENS')?.selected,true);
 const activeTarget=active.core.parameters.find(p=>p.key==='jeans_cells')!;
 assert.equal(activeTarget.parsedValue,160);assert.equal(activeTarget.valueSource,'input');
 assert.equal(active.identity.configRevision,createHash('sha256').update(editedText).digest('hex'));
 assert.equal(active.identity.binarySha256,schema.binarySha256);
 const invalid=editPar(working,'jeans_cells','3');
 assert.ok(parErrors(invalid,currentSchema.parameters).jeans_cells);
 const invalidText=exportDraft(invalid).text;
 assert.ok(invalidText.includes('jeans_cells = 3'));
 const bad=await inspectText(invalidText);
 assert.equal(bad.core.status,'error');
 assert.ok(bad.core.diagnostics.some(d=>d.parameterKey==='jeans_cells'&&d.code==='INVALID_RANGE'));
 const inactiveBad=await inspectText(configure(selfText,{jeans_cells:'3'}));
 assert.equal(inactiveBad.core.status,'error');
 assert.ok(inactiveBad.core.diagnostics.some(d=>d.parameterKey==='jeans_cells'&&d.code==='INVALID_RANGE'));
 const cudaText=configure(editedText,{compute_backend:'cuda',geometry:'cartesian'});
 const cudaActive=await inspectText(cudaText);
 assert.equal(cudaActive.core.status,'ok');
 assert.equal(cudaActive.core.amrIndicators?.choices.find(c=>c.value==='JENS')?.available,true);
 assert.equal(cudaActive.core.amrIndicators?.choices.find(c=>c.value==='JENS')?.selected,true);
 assert.equal(cudaActive.core.parameters.find(p=>p.key==='jeans_cells')?.parsedValue,160);
 assert.equal(cudaActive.core.parameters.find(p=>p.key==='jeans_cells')?.valueSource,'input');
 assert.equal(cudaActive.identity.configRevision,createHash('sha256').update(cudaText).digest('hex'));
 for(const changes of [{compute_backend:'auto'},
  {compute_backend:'cuda',geometry:'cylindrical'},
  {compute_backend:'cuda',geometry:'spherical'},
  {compute_backend:'cuda',gravity_type:'none'}]){
  const rejected=await inspectText(configure(editedText,changes));
  assert.equal(rejected.core.status,'error');
  assert.ok(rejected.core.diagnostics.some(d=>d.parameterKey==='refine_var'&&d.code==='INVALID_REFINEMENT_SELECTION'));
  assert.equal(rejected.core.parameters.find(p=>p.key==='refine_var')?.parsedValue,'JENS');
  assert.equal(rejected.core.parameters.find(p=>p.key==='jeans_cells')?.parsedValue,160);
  assert.equal(rejected.core.parameters.find(p=>p.key==='compute_backend')?.parsedValue,changes.compute_backend);
  assert.equal(rejected.core.execution.simulationReadiness,'not_checked');
  assert.equal(rejected.core.execution.cuda,'not_initialized');
 }
 const outputOnly=await inspectText(configure(selfText,{plt_variables:'DENS,JENS'}));
 assert.equal(outputOnly.core.status,'ok');
 const outputTarget=outputOnly.core.parameters.find(p=>p.key==='jeans_cells')!;
 assert.equal(outputTarget.parsedValue,null);assert.equal(outputTarget.requirement.required,false);
 for(const result of [unselected,missing,active,cudaActive,bad,inactiveBad,outputOnly]){
  assert.equal(result.core.execution.setup,'not_executed');
  assert.equal(result.core.execution.eos,'not_loaded');
  assert.equal(result.core.execution.cuda,'not_initialized');
  assert.equal(result.core.execution.simulationReadiness,'not_checked');
  assert.equal(result.identity.binarySha256,schema.binarySha256);
 }
 assert.equal(await readFile(f.root+'/untouched.par','utf8'),'disk original');
 assert.deepEqual(await readdir(f.root),before);
 console.log(JSON.stringify({status:'PASS',schemaVersion:schema.core.version,binarySha256:schema.binarySha256,
  parameterCount:currentSchema.parameters.length,
  cases:['valid','empty','duplicate-and-syntax','bounded-overflow','JENS-unselected-available',
   'JENS-missing-null','JENS-first-edit-insertion','JENS-one-Undo-Redo','JENS-active-input',
   'JENS-invalid-retained','JENS-inactive-invalid','JENS-Cartesian-CUDA-static-route',
   'JENS-auto-native-CUDA-nonself-rejected','JENS-output-only-no-target'],scientificResources:'not-created',
  scope:'real binary through ConfigurationAdapter; static selected-binary identity, no successful Build claim'},null,2));
}finally{await f.cleanup();}
