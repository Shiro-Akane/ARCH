import {effectiveConfigurationFields} from './configurationIdentityFields.ts';
import {validFieldDeclaration,validMeasureLabels} from '../src/host/plotfileDeclarations.ts';
import type {PlotfileFieldDeclaration} from '../src/host/plotfileDeclarations.ts';
import {copyPointRequest,nativeAxisContains,POINT_BOUNDARY_RULE} from '../src/host/plotfilePoint.ts';
import type {PlotfilePointRequest,PlotfilePointEvidence} from '../src/host/plotfilePoint.ts';
import {copyOverviewRequest,createOverview,MAX_OVERVIEW_BLOCKS,validNativeBlocks} from '../src/host/plotfileOverview.ts';
import type {PlotfileOverviewRequest,PlotfileOverview,NativePlotBlock,NativePlotBlocks} from '../src/host/plotfileOverview.ts';
/**
 * Inspect the current ARCH writer's structure without loading field arrays.
 * This is a local audit primitive, not a result provider: readable HDF5 and
 * stable file identity do not prove writer completion or scientific provenance.
 */
import {constants} from 'node:fs';
import {open,realpath} from 'node:fs/promises';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
import h5 from 'h5wasm/node';
import {sourceEvidenceValid} from '../src/host/plotfileSourceIdentity.ts';
import {copyPlotfileSliceRequest,MAX_SLICE_CELLS} from './plotfileSliceRequest.ts';
import type {PlotfileSliceRequest} from './plotfileSliceRequest.ts';
export type {PlotfileSliceRequest} from './plotfileSliceRequest.ts';

const MAX_FILE_BYTES=64*1024*1024;
const MAX_FIELDS=128;
const MAX_CELLS=10_000_000;

function shapeOf(dataset: unknown, label: string): number[] {
 if(!(dataset instanceof h5.Dataset) || ![0,1].includes(dataset.metadata.type))
  throw new Error('Unsupported numeric dataset: '+label);
 const shape=dataset.shape;
 if(!shape || !shape.length || shape.length>4 || shape.some(n=>!Number.isSafeInteger(n)||n<1))
  throw new Error('Invalid dataset shape: '+label);
 let cells=1;
 for(const n of shape){cells*=n;if(!Number.isSafeInteger(cells)||cells>MAX_CELLS)throw new Error('Dataset exceeds metadata audit budget: '+label);}
 return shape;
}
function scalar(file: InstanceType<typeof h5.File>|InstanceType<typeof h5.Group>|InstanceType<typeof h5.Dataset>, name: string, limit=128): unknown {
 const attribute=file.attrs[name];
 if(!attribute || (attribute.shape && attribute.shape.reduce((a,b)=>a*b,1)!==1))
  throw new Error('Missing or non-scalar plot metadata: '+name);
 // The only string consumed is the writer's bounded geometry identifier.
 if(attribute.metadata.size>limit)throw new Error('Oversized plot metadata: '+name);
 const value=attribute.json_value;
 return Array.isArray(value)&&value.length===1?value[0]:value;
}
function group(file: InstanceType<typeof h5.File>, name: string): InstanceType<typeof h5.Group> {
 const entity=file.get(name);
 if(!(entity instanceof h5.Group))throw new Error('Missing plot group: '+name);
 return entity;
}
function dataset(group: InstanceType<typeof h5.Group>, name: string) {
 const value=group.get(name);
 // External links are returned as ExternalLink objects, never dereferenced here.
 if(!(value instanceof h5.Dataset))throw new Error('Missing local dataset: '+name);
 return value;
}

function fieldDeclaration(d:InstanceType<typeof h5.Dataset>):PlotfileFieldDeclaration|null {
 if(!d.attrs.metadata_version)return null;
 const known=(key:string)=>{const value=scalar(d,key,256);return value==='unknown'?null:value;};
 const value={version:scalar(d,'metadata_version'),unit:known('unit'),centering:scalar(d,'centering'),
  basis:known('basis'),meaning:known('meaning'),unitReason:d.attrs.unit_reason?scalar(d,'unit_reason',256):null};
 if(!validFieldDeclaration(value))throw Error('Invalid recorded field declaration.');
 return value;
}
/** Read and verify a bounded scalar provenance record before returning its digest. */
function identityRecord(e:InstanceType<typeof h5.Group>,name:string,expected:unknown):string {
 const d=dataset(e,name),shape=d.shape;
 if(d.metadata.type!==3||shape&&shape.reduce((a,b)=>a*b,1)!==1)
  throw Error('Invalid formal identity record: '+name);
 const raw=d.value,value=Array.isArray(raw)&&raw.length===1?raw[0]:raw;
 if(typeof value!=='string'||!value.length||Buffer.byteLength(value)>1024*1024||value.includes('\0')||
  typeof expected!=='string'||createHash('sha256').update(value).digest('hex')!==expected)
  throw Error('Formal identity record digest mismatch: '+name);
 return value;
}
/** Decode the producer's typed length-prefix record without reinterpreting floats. */
function typedRecord(record:string):Map<string,{type:string;value:string}> {
 const bytes=Buffer.from(record),values=new Map<string,{type:string;value:string}>();let offset=0;
 const size=()=>{const colon=bytes.indexOf(58,offset);if(colon<offset||colon-offset>8)throw Error('Invalid typed identity length');
  const n=bytes.subarray(offset,colon).toString('ascii');if(!/^(0|[1-9][0-9]*)$/.test(n))throw Error('Invalid typed identity length');
  offset=colon+1;return Number(n);};
 while(offset<bytes.length){const keySize=size();if(keySize===0||keySize>bytes.length-offset)throw Error('Invalid typed identity key');
  const key=bytes.subarray(offset,offset+keySize).toString('utf8');offset+=keySize;
  const type=String.fromCharCode(bytes[offset++]),valueSize=size();
  if(valueSize>bytes.length-offset||!['s','d','i','b','n'].includes(type)||values.has(key))throw Error('Invalid typed identity field');
  const value=bytes.subarray(offset,offset+valueSize).toString('utf8');offset+=valueSize;
  if(bytes[offset++]!==10||type==='d'&&!/^[a-f0-9]{16}$/.test(value)||type==='b'&&!['0','1'].includes(value)||
    type==='n'&&value!==''||type==='i'&&(!/^-?(0|[1-9][0-9]*)$/.test(value)||value==='-0'||
     BigInt(value)<-(1n<<63n)||BigInt(value)>=(1n<<63n))||
    type==='d'&&!Number.isFinite(Buffer.from(value,'hex').readDoubleBE()))throw Error('Invalid typed identity value');
  values.set(key,{type,value});
 }
 return values;
}
/** Preserve exact binary64 field identities for cross-checks against HDF scalar attributes. */
function floatBits(value:number):string {const bytes=Buffer.alloc(8);bytes.writeDoubleBE(value);return bytes.toString('hex');}

function sourceEvidence(file:InstanceType<typeof h5.File>){
 const e=file.get('SourceIdentity');
 if(e===null)return null;
 if(!(e instanceof h5.Group))throw Error('Invalid local SourceIdentity group.');
 const read=(key:string)=>scalar(e,key);
 const known=(key:string)=>{const value=read(key);return value==='unknown'?null:value;};
 const formal=read('version')==='arch-plot-identity-1';
 if(!formal){
  for(const key of ['effective_config_sha256','build_id','source_git_head'])
   if(read(key)!=='unknown')throw Error('Unsupported candidate source identity claim: '+key);
  if(scalar(file,'plot_identity_state')!=='unknown')throw Error('Candidate source identity cannot certify full provenance.');
 }else if(scalar(file,'plot_identity_state')!=='recorded')throw Error('Invalid formal identity state');
 const count=read('species_count');
 if(typeof count!=='number'||!Number.isSafeInteger(count)||count<0||count>128)
  throw Error('Candidate species count exceeds budget.');
 let speciesNames:unknown[]=[];
 if(count){
  const d=dataset(e,'species_names'),shape=d.shape;
  if(d.metadata.type!==3||!shape||shape.length!==1||shape[0]!==count)
   throw Error('Invalid candidate species dataset.');
  const values=d.slice([[0,count]]);
  if(!Array.isArray(values))throw Error('Invalid candidate species representation.');
  speciesNames=values;
 }else if(e.get('species_names')!==null)throw Error('Contradictory zero-count species dataset.');
 let speciesProperties:unknown;
 const propertyNames=['A','Z','gamma','Cv'];
 if(e.attrs.species_properties_version){
  const version=read('species_properties_version'),state=read('species_properties_state');
  if(version!=='checkpoint-species-1')throw Error('Unsupported candidate species properties version.');
  if(state==='recorded'){
   if(count===0&&(!formal||read('eos_type')!=='ideal'))throw Error('Invalid empty recorded species properties.');
   const values:Record<string,number[]>={};
   for(const key of propertyNames){
    // Exact formal gamma-only IdealGas has explicit zero record counts and no
    // constituent datasets. Do not infer known-empty evidence for legacy files.
    if(count===0){
     if(e.get('species_'+key)!==null)throw Error('Contradictory zero-count species properties dataset.');
     values[key]=[];continue;
    }
    const d=dataset(e,'species_'+key),shape=d.shape;
    if(d.metadata.type!==1||d.metadata.size!==8||!shape||shape.length!==1||shape[0]!==count)
     throw Error('Invalid candidate species properties shape/type.');
    const data=d.slice([[0,count]]);
    if(!(data instanceof Float64Array)||!data.every(Number.isFinite))
     throw Error('Invalid candidate species properties values.');
    values[key]=Array.from(data);
   }
   speciesProperties={version,state,source:read('species_properties_source'),values};
  }else if(state==='unknown'){
   if(read('species_properties_source')!=='unknown'||propertyNames.some(key=>e.get('species_'+key)!==null))
    throw Error('Contradictory unknown candidate species properties.');
   speciesProperties={version,state,source:null,values:null,reason:scalar(e,'species_properties_reason',256)};
  }else throw Error('Invalid candidate species properties state.');
 }else if(propertyNames.some(key=>e.get('species_'+key)!==null)||
   ['species_properties_state','species_properties_source','species_properties_reason'].some(key=>e.attrs[key])){
  throw Error('Candidate species properties lack their version.');
 }
 const reasonKeys={effectiveConfigSha256:'effective_config_sha256_reason',
  buildId:'build_id_reason',sourceGitHead:'source_git_head_reason'};
 const reasonCount=Object.values(reasonKeys).filter(key=>e.attrs[key]).length;
 if(reasonCount!==0&&reasonCount!==3)throw Error('Incomplete candidate source identity reasons.');
 const unknownIdentityReasons=reasonCount?Object.fromEntries(Object.entries(reasonKeys)
  .map(([key,attribute])=>[key,scalar(e,attribute,256)])):undefined;
 const gammaAvailable=read('ideal_gamma_available');
 if(gammaAvailable!==0&&gammaAvailable!==1)throw Error('Invalid candidate gamma availability.');
 const evidence={
  version:read('version'),scope:read('scope'),
  caseId:known('case_id'),caseSource:known('case_source'),
  rawConfigSha256:known('raw_config_sha256'),rawConfigSource:known('raw_config_source'),
  binarySha256:known('binary_sha256'),binarySource:known('binary_source'),binaryScope:read('binary_scope'),
  eosType:known('eos_type'),eosSource:known('eos_source'),
  eosTableState:read('eos_table_state'),eosTableSha256:known('eos_table_sha256'),
  idealGamma:gammaAvailable===1?read('ideal_gamma'):null,
  speciesState:read('species_identity_state'),speciesNames,
  ...(speciesProperties===undefined?{}:{speciesProperties}),
  ...(unknownIdentityReasons===undefined?{}:{unknownIdentityReasons}),
  runId:known('run_id'),...(e.attrs.run_id_source?{runIdSource:known('run_id_source')}:{}),
  effectiveConfigSha256:formal?read('effective_config_sha256'):null,buildId:formal?read('build_id'):null,
  sourceGitHead:formal&&read('source_git_head')!=='not-applicable'?read('source_git_head'):null,eosUnitSystem:known('eos_unit_system'),
  ...(formal?{caseSourceSha256:read('case_source_sha256'),effectiveConfigVersion:read('effective_config_version'),
   effectiveConfigSource:read('effective_config_source'),buildIdentityVersion:read('build_identity_version'),
   buildIdentityScope:read('build_identity_scope'),buildSource:read('build_source'),
   sourceManifestSha256:read('source_manifest_sha256'),buildProfileSha256:read('build_profile_sha256'),
   sourceGitDirty:read('source_git_dirty'),sourceGitSource:read('source_git_source'),
   eosIdentityVersion:read('eos_identity_version'),eosIdentitySha256:read('eos_identity_sha256'),
   eosTableIdentityKind:read('eos_table_identity_kind'),resolvedBackend:read('resolved_backend'),recordsVerified:true as const}:{}),
 };
 if(!sourceEvidenceValid(evidence))throw Error(formal?'Invalid formal source evidence.':'Invalid candidate source evidence.');
 if(formal){
  const cfg=typedRecord(identityRecord(e,'effective_config_record',evidence.effectiveConfigSha256));
  const eos=typedRecord(identityRecord(e,'eos_identity_record',evidence.eosIdentitySha256));
  for(const [key,type] of effectiveConfigurationFields)if(cfg.get(key)?.type!==type)throw Error('Incomplete formal effective configuration: '+key);
  for(const name of ['version','case.id','case.compiled-source','resolved-execution.version','backend','geometry-semantics',
   'flux','reconstruction','limiter','time','eos','network','ode','linear','diffusion','boundary-identity'])
   if(cfg.get(name)?.type!=='s')throw Error('Incomplete resolved execution identity: '+name);
  for(const name of ['amr.refine_species_names','io.plot_species_names']){
   const count=Number(cfg.get(name+'.count')?.value);
   if(!Number.isSafeInteger(count)||count<0||count>128)throw Error('Invalid effective configuration selection length');
   for(let i=0;i<count;i++)if(cfg.get(name+'.'+i)?.type!=='s')throw Error('Incomplete effective configuration selection');
  }
  for(const [name,type] of [['version','s'],['type','s'],['accepted-table-fingerprint','s'],['ideal-gamma','d'],['coulomb-multiplier','d']])
   if(eos.get(name)?.type!==type)throw Error('Missing/mistyped formal EOS identity: '+name);
  if(cfg.get('physics.eos_coulomb_mult')?.value!==eos.get('coulomb-multiplier')?.value)throw Error('Contradictory EOS model control');
  const source=identityRecord(e,'source_manifest_record',evidence.sourceManifestSha256);
  const profile=identityRecord(e,'build_profile_record',evidence.buildProfileSha256);
  const expectedBuild=createHash('sha256').update('arch-build-identity-1\n'+evidence.sourceManifestSha256+'\n'+evidence.buildProfileSha256+'\n').digest('hex');
  if(cfg.get('resolved-execution.version')?.value!=='arch-resolved-execution-1'||evidence.buildId!==expectedBuild||!source.startsWith('arch-project-source-manifest-1\n')||
    !profile.startsWith('arch-configured-compiler-profile-1\n')||
    cfg.get('version')?.value!=='arch-effective-configuration-1'||cfg.get('case.id')?.value!==evidence.caseId||
    cfg.get('case.compiled-source')?.value!==evidence.caseSourceSha256||cfg.get('backend')?.value!==evidence.resolvedBackend||
    eos.get('version')?.value!=='arch-eos-identity-1'||eos.get('type')?.value!==evidence.eosType||
    cfg.get('grid.dim')?.value!==String(scalar(file,'dim'))||cfg.get('grid.geometry')?.value!==scalar(file,'geometry')||
    cfg.get('geometry-semantics')?.value!==(scalar(file,'geometry_semantics_revision')===2?'axisymmetric-rz-2':'existing-1')||
    eos.get('accepted-table-fingerprint')?.value!==(evidence.eosTableSha256??'')||
    evidence.eosType==='ideal'&&(eos.get('ideal-gamma')?.value!==floatBits(evidence.idealGamma as number)||
     cfg.get('physics.gamma')?.value!==floatBits(evidence.idealGamma as number)))throw Error('Contradictory formal runtime identity');
  for(const name of ['names','A','Z','gamma','Cv']){
   const prefix='species.'+name+'.',length=eos.get(prefix+'count');
   if(length?.type!=='i'||length.value!==String(count))throw Error('Contradictory formal EOS constituent count: '+name);
   for(const key of eos.keys())if(key.startsWith(prefix)&&key!==prefix+'count'){
    const index=key.slice(prefix.length);
    if(!/^(0|[1-9][0-9]*)$/.test(index)||Number(index)>=count)throw Error('Unexpected formal EOS constituent entry: '+key);
   }
  }
  for(let i=0;i<count;i++){
   if(eos.get('species.names.'+i)?.type!=='s'||eos.get('species.names.'+i)?.value!==evidence.speciesNames[i])throw Error('Contradictory formal species identity');
   for(const key of ['A','Z','gamma','Cv'] as const){
    const p=evidence.speciesProperties;if(p?.state!=='recorded'||eos.get('species.'+key+'.'+i)?.type!=='d'||eos.get('species.'+key+'.'+i)?.value!==floatBits(p.values[key][i]))
     throw Error('Contradictory formal EOS constituent identity');
   }
  }
 }
 return evidence;
}
type NativeHeader={version:string;measureSource:string;measureConvention:string;measureUnit:'cm'|'cm^2'|'cm^3'|null;
 measureNormalization?:string|null;geometry?:string;axes?:string[];axisUnits?:string[];chart?:string};
/** Recognize an explicit native chart; never map legacy cylindrical directly to RZ. */
function nativeHeader(file:InstanceType<typeof h5.File>,shape:number[],geometry:string):NativeHeader|null {
 const entity=file.get('NativeGrid');
 if(entity===null)return null;
 if(!(entity instanceof h5.Group))throw Error('Invalid local NativeGrid group.');
 const version=scalar(entity,'version'),formal=typeof version==='string'&&version.startsWith('arch-native-');
 const rz=version==='arch-native-axisymmetric-rz-2';
 const partialRz=rz&&scalar(file,'plot_publication_version')==='candidate-1'&&
  scalar(group(file,'SourceIdentity'),'version')==='candidate-identity-1';
 const dimension=shape.length-1;
 if(!formal&&(version!=='candidate-cartesian-1'||geometry!=='cartesian'||![1,2].includes(dimension)))
  throw Error('Unsupported candidate native geometry.');
 if(formal&&(!['arch-native-cartesian-1','arch-native-curvilinear-1','arch-native-axisymmetric-rz-2'].includes(String(version))||
   scalar(entity,'native_geometry')!==geometry||version==='arch-native-cartesian-1'&&geometry!=='cartesian'||
   version==='arch-native-curvilinear-1'&&!['cylindrical','spherical'].includes(geometry)||
   rz&&(geometry!=='cylindrical'||dimension!==2||scalar(file,'geometry_semantics_revision')!==2||
     scalar(file,'geometry_chart')!=='axisymmetric-rz')))throw Error('Invalid formal native geometry profile');
 const convention=rz?'full-rotation-axisymmetric-ring':geometry==='cartesian'?
  'active-coordinate-product; inactive-measures-omitted':'GridMetrics-native-coordinate-integral';
 const expected:Record<string,string|number>={centering:'cell',ghost_cells:0,block_kind:'active-leaf',
  center_basis:'cartesian',measure_source:'GridMetrics::CellVolume',measure_convention:convention,
  logical_identity:'file-local level/logical_x1/logical_x2/logical_x3'};
 for(const [name,value] of Object.entries(expected))
  if(scalar(entity,name)!==value)throw Error('Unsupported native metadata: '+name);
 const rawUnit=scalar(entity,'measure_unit'),measureUnit=rawUnit==='unknown'?null:rawUnit;
 const rawNormalization=entity.attrs.measure_normalization?scalar(entity,'measure_normalization'):null;
 const measureNormalization=rawNormalization==='unknown'?null:rawNormalization;
 if(!validMeasureLabels(measureUnit,measureNormalization,dimension))throw Error('Invalid native measure labels.');
 let axes:string[]|undefined,axisUnits:string[]|undefined;
 if(formal){
  axes=[1,2,3].map(axis=>scalar(entity,'x'+axis+'_axis') as string);
  axisUnits=[1,2,3].map(axis=>scalar(entity,'x'+axis+'_unit') as string);
  const expectedAxes=geometry==='cartesian'?['x','y','z']:rz?['r','z','inactive']:
   dimension===3?(geometry==='cylindrical'?['r','z','phi']:['r','theta','phi']):['r','phi','inactive'];
  const expectedUnits:string[]=expectedAxes.map(axis=>['phi','theta'].includes(axis)?'rad':'cm');
  for(let axis=0;axis<3;axis++){
   if(axis>=dimension){expectedAxes[axis]='inactive';expectedUnits[axis]='inactive';}
   if(axes[axis]!==expectedAxes[axis]||axisUnits[axis]!==expectedUnits[axis])throw Error('Invalid native coordinate axes/units');
  }
 }
 const publication:Record<string,string>={plot_publication_version:formal&&!partialRz?'arch-plot-publication-1':'candidate-1',
  plot_publication_state:'complete',plot_publication_method:'checked-close-atomic-replace',plot_storage_order:'x1-fastest'};
 for(const [name,value] of Object.entries(publication))
  if(scalar(file,name)!==value)throw Error('Invalid native publication: '+name);
 if(rz){
  const state=group(file,'NativeState'),w=dataset(entity,'angular_measure');
  for(const [key,value] of Object.entries({unit:'cm^4',meaning:'integral-r-dV',source:'GridMetrics::Rz::AngularMomentumMeasure',normalization:'full_rotation'}))
   if(scalar(w,key)!==value)throw Error('Invalid RZ angular measure semantics');
  const expected={version:partialRz?'candidate-rz-angular-1':'arch-rz-angular-1',state_semantics:'rz-m-phi-j-over-w-v1',
   storage_order:'same-as-Data; x1-fastest',evolved_state:'m_phi only; J/V is derived output'};
  for(const [name,value] of Object.entries(expected))if(scalar(state,name)!==value)throw Error('Invalid RZ recorded state semantics');
  if(scalar(file,'state_semantics')!=='rz-m-phi-j-over-w-v1')throw Error('Invalid RZ root state semantics');
  for(const name of ['m_phi','angular_momentum_density']){
   const d=dataset(state,name),ds=shapeOf(d,'NativeState/'+name),m=name==='m_phi';
   if(d.metadata.type!==1||d.metadata.size!==8||JSON.stringify(ds)!==JSON.stringify(shape))throw Error('Invalid RZ native state shape/type');
   for(const [key,value] of Object.entries({unit:m?'g/(cm^2*s)':'g/(cm*s)',basis:'local-orthonormal-r-z-phi',centering:'cell',
    meaning:m?'J-cell-over-W':'J-cell-over-V',averaging:m?'r-dV-weighted-angular-momentum-component':'native-volume-angular-momentum-density',
    source:m?'FluidState::mom_w':'arch::state::rz_angular_density'}))if(scalar(d,key)!==value)throw Error('Invalid RZ native field semantics');
  }
 }
 const cells=shape.reduce((a,b)=>a*b,1);
 for(const name of ['x1_lower','x1_upper','x2_lower','x2_upper','x3_lower','x3_upper','cell_measure',...(rz?['angular_measure']:[])]){
  const d=dataset(entity,name),ds=shapeOf(d,'NativeGrid/'+name);
  if(d.metadata.type!==1||d.metadata.size!==8||ds.length!==1||ds[0]!==cells)
   throw Error('Native dataset shape/type mismatch: '+name);
 }
 for(const name of ['logical_x1','logical_x2','logical_x3']){
  const d=dataset(entity,name),ds=shapeOf(d,'NativeGrid/'+name);
  if(d.metadata.type!==0||d.metadata.size!==4||ds.length!==1||ds[0]!==shape[0])
   throw Error('Native logical dataset shape/type mismatch: '+name);
 }
 return {version:String(version),measureSource:'GridMetrics::CellVolume',measureConvention:convention,
  measureUnit:measureUnit as 'cm'|'cm^2'|'cm^3'|null,measureNormalization:measureNormalization as string|null,
  ...(formal?{geometry,axes,axisUnits,chart:rz?'axisymmetric-rz':'existing'}:{})};
}
type RawNumber=number|'NaN'|'Infinity'|'-Infinity';
function rawNumbers(value:unknown,expected:number):RawNumber[] {
 if(!ArrayBuffer.isView(value)||value instanceof DataView||value instanceof BigInt64Array||value instanceof BigUint64Array)
  throw Error('Unsupported slice numeric representation.');
 const numbers=Array.from(value as unknown as ArrayLike<number>);
 if(numbers.length!==expected)throw Error('Slice payload length mismatch.');
 return numbers.map(n=>Number.isNaN(n)?'NaN':n===Infinity?'Infinity':n===-Infinity?'-Infinity':n);
}
function readSlice(file:InstanceType<typeof h5.File>,shape:number[],request:PlotfileSliceRequest,native:NativeHeader|null) {
 const {field,block,start,count}=request,cellShape=shape.slice(1);
 if(!Number.isSafeInteger(block)||block<0||block>=shape[0]||start.length!==cellShape.length||count.length!==cellShape.length)
  throw Error('Invalid block or slice dimension.');
 let cells=1;
 for(let axis=0;axis<cellShape.length;axis++){
  if(!Number.isSafeInteger(start[axis])||!Number.isSafeInteger(count[axis])||start[axis]<0||count[axis]<1||start[axis]+count[axis]>cellShape[axis])
   throw Error('Slice bounds outside stored cell shape.');
  cells*=count[axis];if(cells>MAX_SLICE_CELLS)throw Error('Slice exceeds 512-sample budget.');
 }
 const data=dataset(group(file,'Data'),field);
 if(data.metadata.type!==1||![4,8].includes(data.metadata.size))throw Error('Slice requires float32/float64 fields.');
 const values=rawNumbers(data.slice([[block,block+1],...start.map((n,i)=>[n,n+count[i]] as [number,number])]),cells);
 const blockCells=cellShape.reduce((a,b)=>a*b,1),indices:number[]=[];
 for(let n=0;n<cells;n++){
  let local=n,index=0,stride=1;
  for(let axis=cellShape.length-1;axis>=0;axis--){index+=(start[axis]+local%count[axis])*stride;local=Math.floor(local/count[axis]);stride*=cellShape[axis];}
  indices.push(block*blockCells+index);
 }
 const grid=group(file,'Grid'),coordinates:Record<string,RawNumber[]>={};
 for(const axis of ['x','y','z']){
  const coordinate=dataset(grid,axis);
  if(coordinate.metadata.type!==1||![4,8].includes(coordinate.metadata.size))throw Error('Slice requires float32/float64 coordinates.');
  const result:RawNumber[]=[];
  // Each row is contiguous in x1; never read the complete coordinate array.
  for(let n=0;n<cells;n+=count[count.length-1]){
   const length=count[count.length-1],index=indices[n];
   result.push(...rawNumbers(coordinate.slice([[index,index+length]]),length));
  }
  coordinates[axis]=result;
 }
 let nativeCells:null|{
  version:string;identityScope:string;logicalKey:string;level:number;logicalCoordinates:number[];
  lower:Record<string,number[]>;upper:Record<string,number[]>;cellMeasure:number[];
  measureSource:string;measureConvention:string;measureUnit:'cm'|'cm^2'|'cm^3'|null;measureNormalization?:string|null;
  angularMeasure?:number[];mPhi?:number[];angularMomentumDensity?:number[];
 }=null;
 if(native){
  const ng=group(file,'NativeGrid');
  const readRows=(name:string)=>{
   const d=dataset(ng,name),result:number[]=[];
   for(let n=0;n<cells;n+=count[count.length-1]){
    const length=count[count.length-1],index=indices[n];
    const numbers=rawNumbers(d.slice([[index,index+length]]),length);
    if(numbers.some(v=>typeof v!=='number'||!Number.isFinite(v)))throw Error('Nonfinite native cell geometry: '+name);
    result.push(...numbers as number[]);
   }
   return result;
  };
  const lower:Record<string,number[]>={},upper:Record<string,number[]>={};
  for(let axis=0;axis<3;axis++){
   const name='x'+(axis+1);
   lower[name]=readRows(name+'_lower');upper[name]=readRows(name+'_upper');
   for(let n=0;n<cells;n++)
    if(axis<cellShape.length?upper[name][n]<=lower[name][n]:lower[name][n]!==0||upper[name][n]!==0)
     throw Error('Invalid candidate native bounds: '+name);
  }
  const cellMeasure=readRows('cell_measure');
  let angular:Record<string,number[]>={};
  if(native.chart==='axisymmetric-rz'){
   const angularMeasure=readRows('angular_measure');
   if(angularMeasure.some(value=>value<=0))throw Error('Invalid native angular measure');
   const state=group(file,'NativeState'),readState=(name:string)=>{
    const raw=rawNumbers(dataset(state,name).slice([[block,block+1],...start.map((n,i)=>[n,n+count[i]] as [number,number])]),cells);
    if(raw.some(value=>typeof value!=='number'))throw Error('Nonfinite RZ angular state');
    return raw as number[];
   };
   angular={angularMeasure,mPhi:readState('m_phi'),angularMomentumDensity:readState('angular_momentum_density')};
  }
  if(cellMeasure.some(v=>v<=0))throw Error('Invalid native cell measure.');
  const readBlock=(g:InstanceType<typeof h5.Group>,name:string)=>{
   const d=dataset(g,name);
   if(d.metadata.type!==0||d.metadata.size!==4)throw Error('Invalid native logical identity type.');
   const raw=rawNumbers(d.slice([[block,block+1]]),1)[0];
   if(typeof raw!=='number'||!Number.isSafeInteger(raw)||raw<0)throw Error('Invalid native logical identity value.');
   return raw;
  };
  const level=readBlock(grid,'level');
  const logicalCoordinates=['logical_x1','logical_x2','logical_x3'].map(name=>readBlock(ng,name));
  nativeCells={...native,identityScope:'file-local',logicalKey:[level,...logicalCoordinates].join('/'),
   level,logicalCoordinates,lower,upper,cellMeasure,
   ...angular};
 }
 const nonFinite=values.some(v=>typeof v!=='number')||Object.values(coordinates).some(a=>a.some(v=>typeof v!=='number'));
 return {field,block,start:[...start],shape:[...count],order:'x1-fastest',linearIndices:indices,values,coordinates,
  nativeCells,unit:fieldDeclaration(data)?.unit??null,nonFiniteEncoding:'IEEE special values as explicit strings',diagnostics:nonFinite?['NONFINITE_RAW_VALUES']:[]};
}
function readOverview(file:InstanceType<typeof h5.File>,shape:number[],request:PlotfileOverviewRequest):PlotfileOverview {
 const dimension=(shape.length-1) as 1|2,ng=group(file,'NativeGrid'),data=dataset(group(file,'Data'),request.field);
 if(data.metadata.type!==1||![4,8].includes(data.metadata.size))throw Error('Overview requires stored float fields.');
 const total=shape.reduce((a,b)=>a*b,1),blockCells=shape.slice(1).reduce((a,b)=>a*b,1);
 // Reuse dataset objects only within this open file/query; no cross-query cache.
 const boundDatasets=['x1_lower','x1_upper',...(dimension===2?['x2_lower','x2_upper']:[])].map(name=>dataset(ng,name));
 const bounds=(start:number,count:number)=>boundDatasets.map(d=>{
  const raw=rawNumbers(d.slice([[start,start+count]]),count);
  if(raw.some(v=>typeof v!=='number'))throw Error('Nonfinite overview geometry.');
  return raw as number[];
 });
 const emitted=Math.min(shape[0],MAX_OVERVIEW_BLOCKS);
 const nativeBlocks:NativePlotBlock[]=Array.from({length:emitted},(_,index)=>({
  index,firstCellIndex:index*blockCells,level:0,logicalKey:'',logicalCoordinates:[],
  lower:[Infinity,dimension===2?Infinity:0,0],upper:[-Infinity,dimension===2?-Infinity:0,0],
  cellShape:[shape[shape.length-1],dimension===2?shape[1]:1,1],
 }));
 const domain={x:[Infinity,-Infinity] as [number,number],y:dimension===2?[Infinity,-Infinity] as [number,number]:[0,1] as [number,number]};
 // Pass 1 obtains authoritative bounds. No complete geometry array is materialized.
 for(let start=0;start<total;start+=512){
  const b=bounds(start,Math.min(512,total-start));
  for(let i=0;i<b[0].length;i++){
   const block=Math.floor((start+i)/blockCells);
   if(block<emitted){
    const leaf=nativeBlocks[block];
    leaf.lower[0]=Math.min(leaf.lower[0],b[0][i]);leaf.upper[0]=Math.max(leaf.upper[0],b[1][i]);
    if(dimension===2){leaf.lower[1]=Math.min(leaf.lower[1],b[2][i]);leaf.upper[1]=Math.max(leaf.upper[1],b[3][i]);}
   }
   domain.x[0]=Math.min(domain.x[0],b[0][i]);domain.x[1]=Math.max(domain.x[1],b[1][i]);
   if(dimension===2){domain.y[0]=Math.min(domain.y[0],b[2][i]);domain.y[1]=Math.max(domain.y[1],b[3][i]);}
  }
 }
 const identity=(g:InstanceType<typeof h5.Group>,name:string)=>{
  const d=dataset(g,name);
  if(d.metadata.type!==0||d.metadata.size!==4)throw Error('Invalid leaf identity type.');
  const raw=rawNumbers(d.slice([[0,emitted]]),emitted);
  if(raw.some(v=>typeof v!=='number'||!Number.isSafeInteger(v)||v<0||v>0xffffffff))
   throw Error('Invalid leaf identity.');
  return raw as number[];
 };
 const levels=identity(group(file,'Grid'),'level');
 const logical=['logical_x1','logical_x2','logical_x3'].map(name=>identity(ng,name));
 for(const leaf of nativeBlocks){
  leaf.level=levels[leaf.index];leaf.logicalCoordinates=logical.map(axis=>axis[leaf.index]);
  leaf.logicalKey=[leaf.level,...leaf.logicalCoordinates].join('/');
 }
 const blockSummary:NativePlotBlocks={version:'candidate-leaf-outlines-1',identityScope:'file-local',
  kind:'stored-active-leaf',totalBlocks:shape[0],limit:MAX_OVERVIEW_BLOCKS,
  complete:shape[0]<=MAX_OVERVIEW_BLOCKS,blocks:nativeBlocks};
 if(!validNativeBlocks(blockSummary,total,dimension,domain))throw Error('Invalid candidate leaf block records.');
 if(request.viewport&&dimension===1&&JSON.stringify(request.viewport.y)!=='[0,1]')
  throw Error('1D viewport y is an inactive coordinate; use [0,1].');
 const acc=createOverview(request,dimension,request.viewport??domain);
 // Pass 2 batches adjacent complete rows, or x1 segments when a row exceeds
 // 512 cells. Each rectangular field slice aligns with one contiguous native slice.
 const nx=shape[shape.length-1],ny=dimension===2?shape[1]:1;
 const consume=(block:number,row:number,x:number,width:number,rows:number)=>{
  const count=width*rows,start=block*blockCells+row*nx+x,b=bounds(start,count);
  const selection=dimension===1?[[block,block+1],[x,x+width]]:[[block,block+1],[row,row+rows],[x,x+width]];
  const values=rawNumbers(data.slice(selection as [number,number][]),count);
  for(let i=0;i<count;i++){
   const value=values[i];acc.add(start+i,[b[0][i],dimension===2?b[2][i]:0],
    [b[1][i],dimension===2?b[3][i]:1],typeof value==='number'?value:NaN);
  }
 };
 for(let block=0;block<shape[0];block++){
  if(dimension===2&&nx<=512){
   const batchRows=Math.floor(512/nx);
   for(let row=0;row<ny;row+=batchRows)consume(block,row,0,nx,Math.min(batchRows,ny-row));
  }else{
   for(let row=0;row<ny;row++)for(let x=0;x<nx;x+=512)consume(block,row,x,Math.min(512,nx-x),1);
  }
 }
 return {...acc.finish(),globalDomain:domain,nativeBlocks:blockSummary};
}

function readPoint(file:InstanceType<typeof h5.File>,shape:number[],request:PlotfilePointRequest,native:NativeHeader){
 const dimension=shape.length-1;
 if(request.point.length!==dimension)throw Error('Point coordinates must match stored dimension.');
 const ng=group(file,'NativeGrid'),total=shape.reduce((a,b)=>a*b,1);
 // Reuse dataset objects only within this open file/query; no cross-query cache.
 const boundDatasets=['x1_lower','x1_upper',...(dimension===2?['x2_lower','x2_upper']:[])].map(name=>dataset(ng,name));
 const bounds=(start:number,count:number)=>boundDatasets.map(d=>{
  const values=rawNumbers(d.slice([[start,start+count]]),count);
  if(values.some(v=>typeof v!=='number'))throw Error('Nonfinite native point geometry.');
  return values as number[];
 });
 const domain={x:[Infinity,-Infinity] as [number,number],y:dimension===2?[Infinity,-Infinity] as [number,number]:[0,1] as [number,number]};
 for(let start=0;start<total;start+=512){
  const b=bounds(start,Math.min(512,total-start));
  for(let i=0;i<b[0].length;i++){
   domain.x[0]=Math.min(domain.x[0],b[0][i]);domain.x[1]=Math.max(domain.x[1],b[1][i]);
   if(dimension===2){domain.y[0]=Math.min(domain.y[0],b[2][i]);domain.y[1]=Math.max(domain.y[1],b[3][i]);}
  }
 }
 let index=-1,matches=0;
 for(let start=0;start<total;start+=512){
  const b=bounds(start,Math.min(512,total-start));
  for(let i=0;i<b[0].length;i++){
   if(b[1][i]<=b[0][i]||dimension===2&&b[3][i]<=b[2][i])throw Error('Invalid native point cell bounds.');
   if(nativeAxisContains(request.point[0],b[0][i],b[1][i],domain.x[1])&&
    (dimension===1||nativeAxisContains(request.point[1],b[2][i],b[3][i],domain.y[1]))){index=start+i;matches++;}
  }
 }
 if(matches!==1)throw Error(matches?'AMBIGUOUS_NATIVE_CELL: overlapping stored bounds.':'NO_NATIVE_CELL: point outside stored cells or in a gap.');
 const cellShape=shape.slice(1),per=cellShape.reduce((a,b)=>a*b,1),block=Math.floor(index/per);
 let local=index-block*per;const start=Array(cellShape.length).fill(0);
 for(let axis=cellShape.length-1;axis>=0;axis--){start[axis]=local%cellShape[axis];local=Math.floor(local/cellShape[axis]);}
 const payload=readSlice(file,shape,{field:request.field,block,start,count:cellShape.map(()=>1)},native);
 const pointEvidence:PlotfilePointEvidence={version:'candidate-native-point-1',...request,rule:POINT_BOUNDARY_RULE,domain,scannedCells:total,matchCount:1};
 return {payload,pointEvidence};
}

async function auditPlotfile(path:string,request?:PlotfileSliceRequest,overviewRequest?:PlotfileOverviewRequest,pointRequest?:PlotfilePointRequest) {
 if(/\.partial-[A-Za-z0-9]{6}$/.test(path))throw Error('Writer temporary is not a published Plotfile.');
 const source=await open(path,constants.O_RDONLY|constants.O_NOFOLLOW|constants.O_NONBLOCK);
 try {
  // Reject parent-directory symlink swaps before HDF5 reads any bytes.
  if(await realpath('/proc/self/fd/'+source.fd)!==resolve(path))
   throw new Error('Plotfile path identity changed during open.');
  const before=await source.stat({bigint:true});
  if(!before.isFile()||before.size<1n||before.size>BigInt(MAX_FILE_BYTES))
   throw new Error('Choose a regular non-empty plotfile of at most 64 MiB for metadata audit.');
  await h5.ready;
  // Pin the descriptor on Linux: replacement of the selected path cannot switch
  // the HDF5 object between stat/hash/header reads.
  const file=new h5.File('/proc/self/fd/'+source.fd,'r');
  let structure;
  let overview:PlotfileOverview|undefined;
  let pointEvidence:PlotfilePointEvidence|undefined;
  let payload:ReturnType<typeof readSlice>|undefined;
  try {
   if(file.file_id<0n)throw new Error('Could not open HDF5 plotfile.');
   const time=scalar(file,'time'), dimension=scalar(file,'dim'), geometry=scalar(file,'geometry');
   if(typeof time!=='number'||!Number.isFinite(time)||time<0||
      ![1,2,3].includes(Number(dimension))||typeof dimension!=='number'||
      typeof geometry!=='string'||!['cartesian','cylindrical','spherical'].includes(geometry))
    throw new Error('Invalid ARCH plotfile metadata.');
   const grid=group(file,'Grid'),data=group(file,'Data');
   const names=data.keys();
   if(!names.length||names.length>MAX_FIELDS)throw new Error('Invalid field count or field audit budget exceeded.');
   const fields=names.map(name=>{const d=dataset(data,name),declaration=fieldDeclaration(d);return {name,shape:shapeOf(d,name),unit:declaration?.unit??null,declaration};});
   const shape=fields[0].shape;
   if(shape.length!==dimension+1||fields.some(f=>JSON.stringify(f.shape)!==JSON.stringify(shape)))
    throw new Error('Field shapes must match [blocks, ...cellShape] for the selected dimension.');
   const cells=shape.reduce((a,b)=>a*b,1),blocks=shape[0];
   for(const axis of ['x','y','z']){
    const s=shapeOf(dataset(grid,axis),'Grid/'+axis);
    if(s.length!==1||s[0]!==cells)throw new Error('Coordinate shape mismatch: '+axis);
   }
   for(const name of ['level','morton']){
    const s=shapeOf(dataset(grid,name),'Grid/'+name);
    if(s.length!==1||s[0]!==blocks)throw new Error('Block metadata shape mismatch: '+name);
   }
   const candidateSourceIdentity=sourceEvidence(file);
   const candidateNativeGrid=nativeHeader(file,shape,geometry);
   const formal=candidateSourceIdentity?.version==='arch-plot-identity-1';
   if(formal&&(!candidateNativeGrid||!candidateNativeGrid.version.startsWith('arch-native-'))||
     candidateNativeGrid?.version.startsWith('arch-native-')&&!formal&&
      !(candidateNativeGrid.version==='arch-native-axisymmetric-rz-2'&&scalar(file,'plot_publication_version')==='candidate-1'&&candidateSourceIdentity?.version==='candidate-identity-1'))throw Error('Formal publication lacks matching runtime/native identities');
   if(candidateNativeGrid){
    // Candidate writer contract preserves raw binary64 fields and centers.
    // Legacy structure inspection has no such precision claim.
    for(const [g,name,label] of [
     ...names.map(name=>[data,name,'field '+name] as const),
     ...['x','y','z'].map(name=>[grid,name,'coordinate '+name] as const)]){
     const d=dataset(g,name);
     if(d.metadata.type!==1||d.metadata.size!==8)
      throw Error('Candidate native '+label+' requires FP64.');
    }
   }
   if(request){if(!names.includes(request.field))throw Error('Unknown stored field.');payload=readSlice(file,shape,request,candidateNativeGrid);}
   if(pointRequest){
    if(!candidateNativeGrid||geometry!=='cartesian'||![1,2].includes(dimension)||!names.includes(pointRequest.field))throw Error('Point read requires candidate native Cartesian 1D/2D bounds and a stored field.');
    ({payload,pointEvidence}=readPoint(file,shape,pointRequest,candidateNativeGrid));
   }
   if(overviewRequest){
    if(!candidateNativeGrid||geometry!=='cartesian'||![1,2].includes(dimension)||!names.includes(overviewRequest.field))throw Error('Overview requires candidate native Cartesian 1D/2D metadata and a stored field.');
    overview=readOverview(file,shape,overviewRequest);
   }
   const coordinateUnit=grid.attrs.coordinate_unit?scalar(grid,'coordinate_unit'):null;
   if(coordinateUnit!==null&&!['unknown','cm'].includes(String(coordinateUnit)))throw Error('Unsupported recorded coordinate unit.');
   const timeUnit=file.attrs.time_unit?scalar(file,'time_unit'):null;
   if(timeUnit!==null&&!['unknown','s'].includes(String(timeUnit)))throw Error('Unsupported recorded time unit.');
   structure={timeUnit:timeUnit==='s'?'s':null,candidateSourceIdentity,candidateNativeGrid,time,dimension,geometry,blocks,cellShape:shape.slice(1),cells,order:'x1-fastest',fields,
    coordinates:{storedBasis:'cartesian',centering:'cell-center',units:coordinateUnit==='cm'?'cm':null},
    completion:candidateSourceIdentity?.version==='arch-plot-identity-1'&&candidateNativeGrid?
     {state:'complete',reason:'Recognized checked-close atomic publication with validated scoped runtime provenance.'}:
     {state:'unknown',reason:candidateNativeGrid?'Candidate writer publication recognized; scientific contract review remains pending.':'Legacy writer has no recognized completion/publish contract.'},
    scientificIdentity:candidateSourceIdentity?.version==='arch-plot-identity-1'?{
     case:candidateSourceIdentity.caseId,config:candidateSourceIdentity.effectiveConfigSha256,
     build:candidateSourceIdentity.buildId,binary:candidateSourceIdentity.binarySha256,eos:candidateSourceIdentity.eosIdentitySha256}:
     {case:null,config:null,build:null,binary:null,eos:null},
    nativeCellGeometry:candidateSourceIdentity?.version==='arch-plot-identity-1'&&candidateNativeGrid?
     {bounds:'recorded',volume:'recorded'}:{bounds:'unavailable',volume:'unavailable'},
    renderEligible:candidateSourceIdentity?.version==='arch-plot-identity-1'&&!!candidateNativeGrid&&geometry==='cartesian'&&[1,2].includes(dimension),
    diagnostics:[request?'FIELD_SLICE_AUDIT':'METADATA_ONLY',...(formal?['SCOPED_RUNTIME_IDENTITY_RECORDED','NATIVE_CELL_GEOMETRY_RECORDED','NUMERICAL_QUALIFICATION_SEPARATE']:['OUTPUT_COMPLETION_UNVERIFIED',fields.every(f=>f.unit===null)?'UNITS_UNAVAILABLE':'RECORDED_UNITS_REVIEW_PENDING','SCIENTIFIC_IDENTITY_UNAVAILABLE',...(candidateNativeGrid?['CANDIDATE_NATIVE_GRID_REVIEW_PENDING']:['NATIVE_CELL_GEOMETRY_UNAVAILABLE'])])]};
  } finally {if(file.file_id>=0n)file.close();}
  const hash=createHash('sha256'),buffer=Buffer.alloc(64*1024);
  let position=0;
  while(true){const {bytesRead}=await source.read(buffer,0,buffer.length,position);if(!bytesRead)break;hash.update(buffer.subarray(0,bytesRead));position+=bytesRead;if(position>MAX_FILE_BYTES)throw new Error('Plotfile changed beyond audit budget.');}
  const after=await source.stat({bigint:true});
  if(before.size!==after.size||before.mtimeNs!==after.mtimeNs||before.ctimeNs!==after.ctimeNs||position!==Number(before.size))
   throw new Error('Plotfile changed during metadata audit; retry only after authoritative completion.');
  return {schemaVersion:pointRequest?'audit-point-1':overviewRequest?'audit-overview-1':request?'audit-slice-1':'audit-1',payload,overview,pointEvidence,file:{bytes:Number(before.size),sha256:hash.digest('hex'),device:before.dev.toString(),inode:before.ino.toString(),mtimeNs:before.mtimeNs.toString(),ctimeNs:before.ctimeNs.toString()},...structure};
 } finally {await source.close();}
}


/** Read only headers and a streaming file digest; never load field payloads. */
export function inspectPlotfileMetadata(path:string){return auditPlotfile(path);}

/** Local audit primitive only. Production use requires isolated worker ownership. */
export function readPlotfileFieldSlice(path:string,request:PlotfileSliceRequest){
 try{return auditPlotfile(path,copyPlotfileSliceRequest(request));}
 catch(error){return Promise.reject(error);}
}

export function readPlotfileOverview(path:string,request:PlotfileOverviewRequest){
 try{return auditPlotfile(path,undefined,copyOverviewRequest(request));}
 catch(error){return Promise.reject(error);}
}

export function readPlotfilePoint(path:string,request:PlotfilePointRequest){
 try{return auditPlotfile(path,undefined,undefined,copyPointRequest(request));}
 catch(error){return Promise.reject(error);}
}
