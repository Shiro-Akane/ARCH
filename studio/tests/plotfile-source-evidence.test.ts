import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,copyFile,rm,readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {effectiveConfigurationFields} from '../host/configurationIdentityFields.ts';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import h5 from 'h5wasm/node';
import {sourceEvidenceValid} from '../src/host/plotfileSourceIdentity.ts';
import {inspectPlotfileMetadata,readPlotfileFieldSlice,readPlotfilePoint} from '../host/plotfileMetadata.ts';
import {validatePlotfileAudit,validatePlotfilePoint} from '../src/host/plotfileAudit.ts';
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

test('recorded run UUID passes writer-shaped HDF -> reader -> client evidence',async()=>{
 const run='67cd09d8-a208-4ff8-92ad-cd66d58f207f';
 const source='DriverIO output session; OS-generated UUIDv4';
 await fixture(async path=>{
  const metadata=await inspectPlotfileMetadata(path);
  assert.equal(metadata.candidateSourceIdentity?.runId,run);
  assert.equal(metadata.candidateSourceIdentity?.runIdSource,source);
  assert.ok(sourceEvidenceValid(metadata.candidateSourceIdentity));
  for(const change of [{runId:'filename'},{runIdSource:'filename'},{runId:null}]){
   assert.equal(sourceEvidenceValid({...metadata.candidateSourceIdentity,...change}),false);
  }
 },{run_id:run,run_id_source:source});
 for(const change of [{run_id:run},{run_id:run,run_id_source:'filename'}])
  await fixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/source evidence/);},change);
});

test('recorded unknown identity reasons survive HDF reader/client without upgrading provenance',async()=>{
 const reasons={effectiveConfigSha256:'authoritative effective-config identity not supplied to writer',
  buildId:'authoritative Build Manifest identity not supplied to writer',
  sourceGitHead:'authoritative source Git identity not supplied to writer'};
 const attrs={effective_config_sha256_reason:reasons.effectiveConfigSha256,
  build_id_reason:reasons.buildId,source_git_head_reason:reasons.sourceGitHead};
 await fixture(async path=>{
  const metadata=await inspectPlotfileMetadata(path);
  assert.deepEqual(metadata.candidateSourceIdentity?.unknownIdentityReasons,reasons);
  const audit=validatePlotfileAudit({protocolVersion:PROTOCOL_VERSION,projectId:'session',
   relativePath:'candidate.h5',metadata},'session','candidate.h5').audit;
  assert.deepEqual(audit.candidateSourceIdentity?.unknownIdentityReasons,reasons);
  assert.equal(audit.renderEligible,false);
  assert.equal(audit.candidateSourceIdentity?.buildId,null);
 },attrs);
 for(const unknownIdentityReasons of [{...reasons,buildId:''},{...reasons,buildId:' '},
  {...reasons,buildId:'x'.repeat(257)},{...reasons,buildId:'bad\0reason'},
  {buildId:'partial'},{...reasons,extra:'unexpected'}])
  assert.equal(sourceEvidenceValid({...valid,unknownIdentityReasons}),false);
 for(const changes of [{build_id_reason:'partial'}, {...attrs,source_git_head_reason:''},
  {...attrs,build_id_reason:'x'.repeat(257)}])
  await fixture(async path=>{
   await assert.rejects(inspectPlotfileMetadata(path),/reason|metadata|source evidence/i);
  },changes);
});

// Format fixtures exercise publication/identity checks, not scientific solvers.
const sha=(text:string)=>createHash('sha256').update(text).digest('hex');
const bits=(value:number)=>{const b=Buffer.alloc(8);b.writeDoubleBE(value);return b.toString('hex');};
type IdentityField={type:string;value:string};
const encode=(fields:Map<string,IdentityField>)=>Array.from(fields,([name,{type,value}])=>`${Buffer.byteLength(name)}:${name}${type}${Buffer.byteLength(value)}:${value}\n`).join('');
async function formalFixture(run:(path:string)=>Promise<void>,options:{dimension?:number;geometry?:string;rz?:boolean;partialRz?:boolean;partialIdentity?:boolean;candidatePublication?:boolean;gammaOnly?:boolean;
 sourceChange?:(source:InstanceType<typeof h5.Group>)=>void;
 // Optional rectilinear stored bounds exercise format lookup, not physical chart measures.
 pointBounds?:boolean;boundChange?:(axis:number,index:number,upper:boolean,value:number)=>number;
 recordChange?:(config:Map<string,IdentityField>,eos:Map<string,IdentityField>)=>void;attrs?:Record<string,string|number>}={}){
 const dimension=options.dimension??2,geometry=options.geometry??'cartesian',rz=options.rz??false,shape=dimension===1?[1,5]:dimension===2?[1,3,5]:[1,2,3,5];
 const speciesNames=options.gammaOnly?[]:['first','second'],idealGamma=options.gammaOnly?5/3:1.4;
 const count=shape.reduce((a,b)=>a*b,1),dir=await mkdtemp(join(tmpdir(),'arch-formal-plot-')),path=join(dir,'fixture.h5');
 const attributes=(g:InstanceType<typeof h5.Group>|InstanceType<typeof h5.Dataset>,values:Record<string,string|number>)=>{for(const [key,value] of Object.entries(values))g.create_attribute(key,value);};
 try{
  const f=new h5.File(path,'w');
  try{
   attributes(f,{time:.25,time_unit:'s',dim:dimension,geometry,geometry_semantics_revision:rz?2:1,geometry_chart:rz?'axisymmetric-rz':'existing',...(rz?{state_semantics:'rz-m-phi-j-over-w-v1'}:{}),
    plot_identity_state:options.partialRz||options.partialIdentity?'unknown':'recorded',plot_publication_version:options.partialRz||options.candidatePublication?'candidate-1':'arch-plot-publication-1',plot_publication_state:'complete',
    plot_publication_method:'checked-close-atomic-replace',plot_storage_order:'x1-fastest'});
   const grid=f.create_group('Grid');grid.create_attribute('coordinate_unit','cm');
   for(const axis of ['x','y','z'])grid.create_dataset({name:axis,data:Float64Array.from({length:count},(_,i)=>i+.25)});
   for(const name of ['level','morton'])grid.create_dataset({name,data:new Int32Array([0])});
   const data=f.create_group('Data').create_dataset({name:'DENS',data:Float64Array.from({length:count},(_,i)=>i+1e-6),shape});
   attributes(data,{metadata_version:'arch-field-1',unit:'g/cm^3',centering:'cell',basis:'scalar',meaning:'mass_density'});
   const config=new Map<string,IdentityField>(effectiveConfigurationFields.map(([key,type])=>[key,{type,value:type==='d'?bits(0):type==='s'?'':'0'}]));
   const set=(key:string,type:string,value:string)=>config.set(key,{type,value});
   set('version','s','arch-effective-configuration-1');set('case.id','s','SyntheticIoFixture');set('case.compiled-source','s','c'.repeat(64));
   set('grid.dim','i',String(dimension));set('grid.geometry','s',geometry);set('physics.eos_type','s','ideal');
   set('physics.eos_coulomb_mult','d',bits(1));set('physics.gamma','d',bits(idealGamma));
   set('resolved-execution.version','s','arch-resolved-execution-1');set('backend','s','cpu');set('geometry-semantics','s',rz?'axisymmetric-rz-2':'existing-1');
   for(const key of ['flux','reconstruction','limiter','time','eos','network','ode','linear','diffusion','boundary-identity'])set(key,'s',key==='eos'?'ideal':'test-'+key);
   const eos=new Map<string,IdentityField>();
   for(const [key,value] of Object.entries({version:'arch-eos-identity-1',type:'ideal','accepted-table-fingerprint':''}))eos.set(key,{type:'s',value});
   eos.set('ideal-gamma',{type:'d',value:bits(idealGamma)});eos.set('coulomb-multiplier',{type:'d',value:bits(1)});
   eos.set('species.names.count',{type:'i',value:String(speciesNames.length)});
   speciesNames.forEach((value,i)=>eos.set('species.names.'+i,{type:'s',value}));
   for(const [key,values] of Object.entries(properties.values)){
    eos.set('species.'+key+'.count',{type:'i',value:String(speciesNames.length)});
    if(!options.gammaOnly)values.forEach((value,i)=>eos.set('species.'+key+'.'+i,{type:'d',value:bits(value)}));
   }
   options.recordChange?.(config,eos);
   const configText=encode(config),eosText=encode(eos),source='arch-project-source-manifest-1\nfixture.cpp\t'+'c'.repeat(64)+'\n';
   const profile='arch-configured-compiler-profile-1\nfixture=1\nselected-configuration=Release\n',build=sha('arch-build-identity-1\n'+sha(source)+'\n'+sha(profile)+'\n');
   const id=f.create_group('SourceIdentity');
   attributes(id,{version:'arch-plot-identity-1',scope:'resolved-runtime',case_id:'SyntheticIoFixture',case_source:'ConfigurationInput.case_id',case_source_sha256:'c'.repeat(64),
    raw_config_sha256:'a'.repeat(64),raw_config_source:'ConfigurationInput.raw_text; exact parser bytes',binary_sha256:'b'.repeat(64),binary_source:'Linux /proc/self/exe',binary_scope:'main-executable-only',
    eos_type:'ideal',eos_source:'resolved-runtime-checkpoint-provenance',eos_table_state:'not-applicable',eos_table_sha256:'unknown',ideal_gamma_available:1,ideal_gamma:idealGamma,
    species_identity_state:'recorded',species_count:speciesNames.length,...propertyAttrs,run_id:'67cd09d8-a208-4ff8-92ad-cd66d58f207f',run_id_source:'DriverIO output session; OS-generated UUIDv4',
    effective_config_version:'arch-effective-configuration-1',effective_config_sha256:sha(configText),effective_config_source:'immutable-runtime-config-and-resolved-plan',
    build_identity_version:'arch-build-identity-1',build_identity_scope:'project-source-and-configured-compiler-profile',build_id:build,build_source:'CMake-embedded-project-source-and-compiler-profile',
    source_manifest_sha256:sha(source),build_profile_sha256:sha(profile),source_git_head:'not-applicable',source_git_dirty:'unknown',source_git_source:'optional-build-time-annotation',
    eos_identity_version:'arch-eos-identity-1',eos_identity_sha256:sha(eosText),eos_table_identity_kind:'not-applicable',resolved_backend:'cpu',eos_unit_system:'cgs',
    ...(options.partialRz||options.partialIdentity?{version:'candidate-identity-1',scope:'partial',effective_config_sha256:'unknown',build_id:'unknown',source_git_head:'unknown'}:{}),...options.attrs});
   if(speciesNames.length){id.create_dataset({name:'species_names',data:speciesNames});writeProperties(id);}
   options.sourceChange?.(id);
   if(!options.partialRz&&!options.partialIdentity)for(const [name,text] of Object.entries({effective_config_record:configText,eos_identity_record:eosText,source_manifest_record:source,build_profile_record:profile}))id.create_dataset({name,data:text});
   const native=f.create_group('NativeGrid'),axes=geometry==='cartesian'?['x','y','z']:rz?['r','z','inactive']:dimension===3?(geometry==='cylindrical'?['r','z','phi']:['r','theta','phi']):['r','phi','inactive'];
   attributes(native,{version:rz?'arch-native-axisymmetric-rz-2':geometry==='cartesian'?'arch-native-cartesian-1':'arch-native-curvilinear-1',native_geometry:geometry,centering:'cell',ghost_cells:0,
    block_kind:'active-leaf',center_basis:'cartesian',measure_source:'GridMetrics::CellVolume',measure_convention:rz?'full-rotation-axisymmetric-ring':geometry==='cartesian'?'active-coordinate-product; inactive-measures-omitted':'GridMetrics-native-coordinate-integral',
    measure_unit:rz||dimension===3?'cm^3':dimension===1?'cm':'cm^2',measure_normalization:rz?'full_rotation':dimension===3?'full_volume':dimension===1?'per_unit_transverse_area':'per_unit_transverse_length',
    logical_identity:'file-local level/logical_x1/logical_x2/logical_x3'});
   for(let axis=0;axis<3;axis++){
    const label=axis<dimension?axes[axis]:'inactive',unit=axis>=dimension?'inactive':['theta','phi'].includes(label)?'rad':'cm';
    native.create_attribute('x'+(axis+1)+'_axis',label);native.create_attribute('x'+(axis+1)+'_unit',unit);
    // x1 is fastest in actual Data storage. Defaults retain every older fixture byte.
    const extents=shape.slice(1).reverse(),stride=extents.slice(0,axis).reduce((a,b)=>a*b,1);
    const bound=(upper:boolean)=>Float64Array.from({length:count},(_,index)=>{
     const ordinal=axis<dimension?Math.floor(index/stride)%extents[axis]:0;
     const value=axis<dimension?1+(options.pointBounds?ordinal*.5:0)+(upper ? .5 : 0):0;
     return options.boundChange?.(axis,index,upper,value)??value;
    });
    native.create_dataset({name:'x'+(axis+1)+'_lower',data:bound(false)});
    native.create_dataset({name:'x'+(axis+1)+'_upper',data:bound(true)});
    native.create_dataset({name:'logical_x'+(axis+1),data:new Int32Array([0])});
   }
   native.create_dataset({name:'cell_measure',data:new Float64Array(count).fill(.5**dimension)});
   if(rz){
    const w=native.create_dataset({name:'angular_measure',data:new Float64Array(count).fill(.5)});
    attributes(w,{unit:'cm^4',meaning:'integral-r-dV',source:'GridMetrics::Rz::AngularMomentumMeasure',normalization:'full_rotation'});
    const state=f.create_group('NativeState');
    attributes(state,{version:options.partialRz?'candidate-rz-angular-1':'arch-rz-angular-1',state_semantics:'rz-m-phi-j-over-w-v1',storage_order:'same-as-Data; x1-fastest',evolved_state:'m_phi only; J/V is derived output'});
    for(const name of ['m_phi','angular_momentum_density']){
     const m=name==='m_phi',d=state.create_dataset({name,data:new Float64Array(count).fill(m?2:4),shape});
     attributes(d,{unit:m?'g/(cm^2*s)':'g/(cm*s)',basis:'local-orthonormal-r-z-phi',centering:'cell',meaning:m?'J-cell-over-W':'J-cell-over-V',
      averaging:m?'r-dV-weighted-angular-momentum-component':'native-volume-angular-momentum-density',source:m?'FluidState::mom_w':'arch::state::rz_angular_density'});
    }
   }
  }finally{f.close();}
  await run(path);
 }finally{await rm(dir,{recursive:true,force:true});}
}
test('formal scoped provenance completes publication with Cartesian 1D/2D/3D exact native slices',async()=>{
 for(const dimension of [1,2,3])await formalFixture(async path=>{
  const metadata=await inspectPlotfileMetadata(path);assert.equal(metadata.completion.state,'complete');assert.equal(metadata.renderEligible,dimension<3);
  assert.equal(metadata.candidateSourceIdentity?.sourceGitHead,null);assert.equal(metadata.candidateSourceIdentity?.recordsVerified,true);
  const response={protocolVersion:PROTOCOL_VERSION,projectId:'session',relativePath:'formal.h5',metadata};
  assert.equal(validatePlotfileAudit(response,'session','formal.h5').audit.scientificIdentity.case,'SyntheticIoFixture');
  const selection={field:'DENS',block:0,start:new Array(dimension).fill(0),count:new Array(dimension).fill(1)},result=await readPlotfileFieldSlice(path,selection);
  const slice=validatePlotfileAudit({...response,result},'session','formal.h5',selection,metadata.file.sha256).audit;
  assert.deepEqual(slice.payload?.values,[1e-6]);assert.deepEqual(slice.payload?.nativeCells?.cellMeasure,[.5**dimension]);
  assert.equal(slice.payload?.nativeCells?.measureUnit,dimension===1?'cm':dimension===2?'cm^2':'cm^3');
  assert.throws(()=>validatePlotfileAudit({...response,metadata:{...metadata,scientificIdentity:{...metadata.scientificIdentity,config:'bad'}}},'session','formal.h5'),/mismatch/);
 },{dimension});
});
test('formal gamma-only IdealGas preserves known zero composition through Host and client',async()=>{
 await formalFixture(async path=>{
  const metadata=await inspectPlotfileMetadata(path),identity=metadata.candidateSourceIdentity;
  assert.equal(metadata.completion.state,'complete');assert.equal(identity?.idealGamma,5/3);
  assert.equal(identity?.speciesState,'recorded');assert.deepEqual(identity?.speciesNames,[]);
  assert.deepEqual(identity?.speciesProperties,{...properties,values:{A:[],Z:[],gamma:[],Cv:[]}});
  assert.ok(sourceEvidenceValid(identity));
  const response={protocolVersion:PROTOCOL_VERSION,projectId:'session',relativePath:'gamma-only.h5',metadata};
  assert.deepEqual(validatePlotfileAudit(response,'session','gamma-only.h5').audit.candidateSourceIdentity?.speciesNames,[]);
  for(const change of [{idealGamma:1},{idealGamma:NaN},{speciesState:'unknown'},
   {speciesProperties:{...properties,values:{A:[1],Z:[],gamma:[],Cv:[]}}},
   {eosType:'helmholtz',idealGamma:null,eosTableState:'recorded',eosTableSha256:'c'.repeat(64),eosTableIdentityKind:'accepted-table-content'}])
   assert.equal(sourceEvidenceValid({...identity,...change}),false);
  assert.equal(sourceEvidenceValid({...valid,speciesNames:[],speciesProperties:{...properties,values:{A:[],Z:[],gamma:[],Cv:[]}}}),false);
 },{gammaOnly:true,dimension:1});
});
test('known empty composition rejects absent counts, extra entries/datasets and contradictory EOS/gamma',async()=>{
 const changes:((config:Map<string,IdentityField>,eos:Map<string,IdentityField>)=>void)[]=[
  (_c,e)=>{e.delete('species.A.count');},(_c,e)=>{e.set('species.Z.count',{type:'i',value:'1'});},
  (_c,e)=>{e.set('species.names.0',{type:'s',value:'invented'});},
  (_c,e)=>{e.set('species.Cv.0',{type:'d',value:bits(1)});},
  (_c,e)=>{e.set('species.A.count',{type:'s',value:'0'});},
  (_c,e)=>{e.set('ideal-gamma',{type:'d',value:bits(1.4)});},
  (_c,e)=>{e.set('ideal-gamma',{type:'s',value:bits(5/3)});},
  (_c,e)=>{e.set('coulomb-multiplier',{type:'s',value:bits(1)});},
  c=>{c.set('physics.gamma',{type:'d',value:bits(1.4)});},
 ];
 for(const recordChange of changes)await formalFixture(async path=>{
  await assert.rejects(inspectPlotfileMetadata(path),/formal|constituent|identity|EOS/i);
 },{gammaOnly:true,recordChange});
 for(const sourceChange of [(source:InstanceType<typeof h5.Group>)=>source.create_dataset({name:'species_names',data:['invented']}),
  (source:InstanceType<typeof h5.Group>)=>source.create_dataset({name:'species_A',data:new Float64Array([1])})])
  await formalFixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/zero-count species/i);},{gammaOnly:true,sourceChange});
 await formalFixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/empty recorded|formal source evidence/i);},{
  gammaOnly:true,attrs:{eos_type:'helmholtz',ideal_gamma_available:0,eos_table_state:'recorded',eos_table_sha256:'c'.repeat(64),eos_table_identity_kind:'accepted-table-content'},
  recordChange:(config,eos)=>{config.set('physics.eos_type',{type:'s',value:'helmholtz'});config.set('eos',{type:'s',value:'helmholtz'});
   eos.set('type',{type:'s',value:'helmholtz'});eos.set('accepted-table-fingerprint',{type:'s',value:'c'.repeat(64)});},
 });
});
test('formal digests cannot certify incomplete, nonfinite or contradictory config/EOS records',async()=>{
 const changes:((config:Map<string,IdentityField>,eos:Map<string,IdentityField>)=>void)[]=[
  c=>{c.delete('physics.diffusion.alpha_therm');},c=>{c.delete('flux');},
  c=>{c.set('numerics.cfl',{type:'d',value:'7ff0000000000000'});},c=>{c.set('grid.nblockx1',{type:'i',value:'9223372036854775808'});},
  c=>{c.set('io.plot_species_names.count',{type:'i',value:'1'});},c=>{c.set('grid.dim',{type:'i',value:'3'});},
  c=>{c.set('physics.eos_coulomb_mult',{type:'d',value:bits(0)});},(_c,e)=>{e.set('species.Cv.0',{type:'d',value:bits(2)});},
 ];
 for(const recordChange of changes)await formalFixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/formal|identity|configuration|EOS|typed/i);},{recordChange});
 for(const attrs of [{effective_config_sha256:'e'.repeat(64)},{build_id:'e'.repeat(64)},{case_id:'another-case'}])
  await formalFixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/formal|identity|digest/i);},{attrs});
});
test('formal 2D cylindrical polar is retired and rejected while spherical 2D polar stays a valid formal profile',async()=>{
 await formalFixture(async path=>{
  await assert.rejects(inspectPlotfileMetadata(path),/Invalid formal native geometry profile/);
 },{geometry:'cylindrical'});
 await formalFixture(async path=>{
  const metadata=await inspectPlotfileMetadata(path);assert.equal(metadata.renderEligible,false);
  assert.deepEqual(metadata.candidateNativeGrid?.axes,['r','phi','inactive']);assert.equal(metadata.candidateNativeGrid?.chart,'existing');
  assert.equal(validatePlotfileAudit({protocolVersion:PROTOCOL_VERSION,projectId:'session',relativePath:'formal.h5',metadata},'session','formal.h5').audit.completion.state,'complete');
 },{geometry:'spherical'});
});
test('Core and Host use identical frozen effective-config field coverage',async()=>{
 const text=await readFile(new URL('../../src/core/config/ConfigurationIdentityFields.inc',import.meta.url),'utf8');
 const fields=Array.from(text.matchAll(/ARCH_CONFIGURATION_IDENTITY_FIELD\("([^"]+)",'([^']+)'\)/g),match=>[match[1],match[2]]);
 assert.deepEqual(fields,effectiveConfigurationFields);
});

test('explicit formal RZ exposes exact stored W/m_phi/J-over-V without renderer or numerical qualification claims',async()=>{
 await formalFixture(async path=>{
  const selection={field:'DENS',block:0,start:[0,0],count:[1,1]},metadata=await inspectPlotfileMetadata(path),result=await readPlotfileFieldSlice(path,selection);
  const audit=validatePlotfileAudit({protocolVersion:PROTOCOL_VERSION,projectId:'session',relativePath:'rz.h5',result},'session','rz.h5',selection,metadata.file.sha256).audit;
  assert.equal(audit.renderEligible,false);assert.equal(audit.completion.state,'complete');
  assert.deepEqual(audit.candidateNativeGrid?.axes,['r','z','inactive']);assert.equal(audit.candidateNativeGrid?.measureNormalization,'full_rotation');
  assert.deepEqual(audit.payload?.nativeCells?.angularMeasure,[.5]);assert.deepEqual(audit.payload?.nativeCells?.mPhi,[2]);
  assert.deepEqual(audit.payload?.nativeCells?.angularMomentumDensity,[4]);
 },{geometry:'cylindrical',rz:true});
});

test('known low-level RZ revision2 remains explicitly partial Inspector-only and rejects crossed publication identities',async()=>{
 await formalFixture(async path=>{
  const metadata=await inspectPlotfileMetadata(path),selection={field:'DENS',block:0,start:[0,0],count:[1,1]},result=await readPlotfileFieldSlice(path,selection);
  assert.equal(metadata.completion.state,'unknown');assert.equal(metadata.renderEligible,false);assert.equal(metadata.scientificIdentity.config,null);
  const audit=validatePlotfileAudit({protocolVersion:PROTOCOL_VERSION,projectId:'session',relativePath:'partial-rz.h5',result},'session','partial-rz.h5',selection,metadata.file.sha256).audit;
  assert.deepEqual(audit.payload?.nativeCells?.mPhi,[2]);assert.equal(audit.candidateSourceIdentity?.scope,'partial');
 },{geometry:'cylindrical',rz:true,partialRz:true});
 for(const options of [{partialIdentity:true},{geometry:'cylindrical',rz:true,candidatePublication:true}])
  await formalFixture(async path=>{await assert.rejects(inspectPlotfileMetadata(path),/publication|identity|native|semantics/i);},options);
});

// These complete formal HDF fixtures verify stored-coordinate wire semantics only.
// Their synthetic measures and identity records do not certify Core writer science.
test('formal 3D point queries use x1-fastest bounds including final x3 and global maxima',async()=>{
 await formalFixture(async path=>{
  const metadata=await inspectPlotfileMetadata(path);assert.equal(metadata.renderEligible,false);
  const identity={protocolVersion:PROTOCOL_VERSION,projectId:'point',relativePath:'formal-3d.h5'};
  for(const [point,index,start] of [
   [[1.25,1.25,1.75],15,[1,0,0]],[[1.5,1.25,1.25],1,[0,0,1]],[[3.5,2.5,2],29,[1,2,4]],
  ] as [number[],number,number[]][]){
   const request={field:'DENS',point},result=await readPlotfilePoint(path,request);
   assert.deepEqual(result.payload?.linearIndices,[index]);assert.deepEqual(result.payload?.values,[index+1e-6]);
   assert.deepEqual(result.payload?.start,start);assert.deepEqual(result.pointEvidence?.domain,{x:[1,3.5],y:[1,2.5],z:[1,2]});
   assert.equal(result.pointEvidence?.scannedCells,30);
   assert.equal(validatePlotfilePoint({...identity,result},'point','formal-3d.h5',request,metadata.file.sha256).audit.renderEligible,false);
   assert.throws(()=>validatePlotfilePoint({...identity,result:{...result,pointEvidence:{...result.pointEvidence,domain:{x:[1,3.5],y:[1,2.5]}}}},
    'point','formal-3d.h5',request,metadata.file.sha256),/point/);
  }
  await assert.rejects(readPlotfilePoint(path,{field:'DENS',point:[1.25,1.25]}),/dimension/);
  await assert.rejects(readPlotfilePoint(path,{field:'DENS',point:[1.25,1.25,2.25]}),/NO_NATIVE_CELL/);
  await assert.rejects(readPlotfilePoint(path,{field:'DENS',point:[1.25,1.25,NaN]}),/finite/);
  await assert.rejects(readPlotfilePoint(path,{field:'missing',point:[1.25,1.25,1.25]}),/stored field/);
 },{dimension:3,pointBounds:true});
});
test('formal native chart point queries retain raw RZ W and spherical bounds without renderer promotion',async()=>{
 for(const options of [{dimension:2,geometry:'cylindrical',rz:true},{dimension:3,geometry:'spherical'},{dimension:3,geometry:'cylindrical'}])
  await formalFixture(async path=>{
   const point=options.dimension===2?[3.5,2.5]:[3.5,2.5,2],request={field:'DENS',point};
   const result=await readPlotfilePoint(path,request),native=result.payload?.nativeCells;
   assert.deepEqual(result.payload?.linearIndices,[options.dimension===2?14:29]);assert.equal(result.renderEligible,false);
   assert.equal(result.completion.state,'complete');assert.deepEqual(native?.lower.x1,[3]);
   if(options.rz){assert.deepEqual(native?.angularMeasure,[.5]);assert.deepEqual(native?.mPhi,[2]);assert.deepEqual(native?.angularMomentumDensity,[4]);}
   else{assert.equal(native?.angularMeasure,undefined);assert.deepEqual(native?.lower.x3,[1.5]);}
   const identity={protocolVersion:PROTOCOL_VERSION,projectId:'point',relativePath:'native.h5',result};
   assert.deepEqual(validatePlotfilePoint(identity,'point','native.h5',request,result.file.sha256).audit.payload?.values,result.payload?.values);
  },{...options,pointBounds:true});
 await formalFixture(async path=>{await assert.rejects(readPlotfilePoint(path,{field:'DENS',point:[1.25,1.25]}),/formal native bounds/);},
  {dimension:2,geometry:'cylindrical',rz:true,partialRz:true,pointBounds:true});
});
test('formal 3D point scan rejects overlapping, missing and malformed native cell bounds',async()=>{
 await formalFixture(async path=>{await assert.rejects(readPlotfilePoint(path,{field:'DENS',point:[1.25,1.25,1.25]}),/AMBIGUOUS_NATIVE_CELL/);},
  {dimension:3,pointBounds:true,boundChange:(axis,index,upper,value)=>index===1?(upper?1.5:1):value});
 await formalFixture(async path=>{await assert.rejects(readPlotfilePoint(path,{field:'DENS',point:[2.25,1.25,1.25]}),/NO_NATIVE_CELL/);},
  {dimension:3,pointBounds:true,boundChange:(axis,index,_upper,value)=>axis===0&&index%5>=2?value+.5:value});
 for(const malformed of [NaN,Infinity])await formalFixture(async path=>{
  await assert.rejects(readPlotfilePoint(path,{field:'DENS',point:[1.25,1.25,1.25]}),/Nonfinite native point geometry/);
 },{dimension:3,pointBounds:true,boundChange:(axis,index,upper,value)=>axis===2&&index===29&&!upper?malformed:value});
 await formalFixture(async path=>{await assert.rejects(readPlotfilePoint(path,{field:'DENS',point:[1.25,1.25,1.25]}),/Invalid native point cell bounds/);},
  {dimension:3,pointBounds:true,boundChange:(axis,index,upper,value)=>axis===2&&index===29&&upper?1.5:value});
});

test('formal 1D point retains inactive y convention and the exact global endpoint owner',async()=>{
 await formalFixture(async path=>{
  const request={field:'DENS',point:[3.5]},result=await readPlotfilePoint(path,request);
  assert.deepEqual(result.payload?.linearIndices,[4]);assert.deepEqual(result.payload?.values,[4+1e-6]);
  assert.deepEqual(result.pointEvidence?.domain,{x:[1,3.5],y:[0,1]});
  assert.equal(validatePlotfilePoint({protocolVersion:PROTOCOL_VERSION,projectId:'point',relativePath:'formal-1d.h5',result},
   'point','formal-1d.h5',request,result.file.sha256).audit.renderEligible,true);
 },{dimension:1,pointBounds:true});
});
