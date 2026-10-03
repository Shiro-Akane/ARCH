import {PROTOCOL_VERSION} from './contracts.ts';
import type {CorePreview,PreviewIdentity,RealPreviewResult,PreviewStatus,ModelCapability} from './previewContracts.ts';
export function record(v:unknown):v is Record<string,unknown>{return !!v&&typeof v==='object'&&!Array.isArray(v);}
function need(ok:unknown,message:string):asserts ok {if(!ok)throw new Error('Invalid Preview response: '+message);}
function text(v:unknown,max=256):v is string{return typeof v==='string'&&v.length<=max;}
export function validateCorePreview(v:unknown,id:Pick<PreviewIdentity,'requestId'|'caseId'|'configRevision'>,count?:number|number[]):CorePreview {
 need(record(v)&&v.schemaVersion==='1.0'&&v.kind==='initial-state-preview'&&['ok','error'].includes(String(v.status))&&text(v.stage),'schema/status');
 need(Array.isArray(v.diagnostics)&&v.diagnostics.length<=128,'diagnostics');
 for(const d of v.diagnostics)need(record(d)&&['info','warning','error'].includes(String(d.severity))&&text(d.code)&&text(d.message,65536),'diagnostic');
 if(v.status==='error'&&v.stage==='input'&&v.identity===null){need(v.data===null,'error data');return v as unknown as CorePreview;}
 need(record(v.identity)&&v.identity.requestId===id.requestId&&v.identity.caseId===id.caseId&&v.identity.configRevision===id.configRevision,'identity');
 need(record(v.execution)&&v.execution.previewBackend==='cpu'&&v.execution.timeStepping==='not_executed'&&v.execution.scientificOutput==='not_created'&&v.execution.simulationReadiness==='not_checked','execution');
 if(v.state!==undefined&&v.state!==null){need(record(v.state),'state');const state=v.state;
  if(state.grid!=null){need(record(state.grid)&&Array.isArray(state.grid.axes)&&state.grid.axes.length<=3,'grid state');for(const a of state.grid.axes)need(record(a)&&text(a.name)&&typeof a.min==='number'&&Number.isFinite(a.min)&&typeof a.max==='number'&&Number.isFinite(a.max),'grid axis');}
  if(state.eos!=null)need(record(state.eos)&&(state.eos.resolved===null||text(state.eos.resolved))&&text(state.eos.status),'EOS state');
  if(state.species!=null)need(Array.isArray(state.species)&&state.species.length<=1024&&state.species.every(s=>record(s)&&text(s.name)),'species state');
  if(state.amr!=null)need(record(state.amr)&&Number.isInteger(state.amr.minLevel)&&Number.isInteger(state.amr.maxLevel),'AMR state');
 }
 validateParameterExtensions(v);
 if(v.status==='error'){need(v.data===null,'error data');return v as unknown as CorePreview;}
 const d=v.data;need(record(d)&&(d.dimension===1||d.dimension===2||d.dimension===3),'dimension');
 const dim=Number(d.dimension);need(d.kind===(dim===1?'line':dim===2?'grid':'volume'),'kind');
 if(record(v.state)&&record(v.state.grid))need(v.state.grid.dimension===dim,'grid/data dimension');
 const maxTotal=dim===1?4096:dim===2?65536:32768,maxAxis=dim===1?4096:dim===2?256:64;
 const s=d.sampling;need(record(s)&&s.kind==='uniform'&&s.valueLocation==='init-sample'&&s.position==='bin-center'&&s.order==='x1-fastest'&&Number.isInteger(s.count)&&Number(s.count)>=2&&Number(s.count)<=maxTotal&&Array.isArray(s.shape)&&s.shape.length===dim,'sampling');
 const shape=s.shape as number[];need(shape.every(n=>Number.isInteger(n)&&n>=2&&n<=maxAxis)&&shape.reduce((a,b)=>a*b,1)===s.count,'sampling shape');
 if(count!==undefined)need(Array.isArray(count)?JSON.stringify(count)===JSON.stringify(shape):count===s.count,'requested sampling');
 if(s.fixedCoordinates!==undefined)need(Array.isArray(s.fixedCoordinates)&&s.fixedCoordinates.length<=3&&s.fixedCoordinates.every(c=>record(c)&&text(c.name)&&typeof c.value==='number'&&Number.isFinite(c.value)&&(c.unit===null||text(c.unit))),'fixed coordinates');
 if(id.caseId==='CellularDet')need(dim===2&&Array.isArray(s.fixedCoordinates)&&s.fixedCoordinates.length===1&&record(s.fixedCoordinates[0])&&s.fixedCoordinates[0].name==='x3'&&s.fixedCoordinates[0].value===0,'Cellular inactive coordinate');
 if(d.coordinates!==undefined){
  const c=d.coordinates;need(record(c)&&c.version==='1'&&c.basis==='native-grid'&&c.velocityBasis==='native-orthonormal'&&['cartesian','cylindrical','spherical'].includes(String(c.geometry))&&['uniform-state','spatial-samples'].includes(String(c.representation))&&record(c.metadata)&&c.metadata.dimension===dim&&c.metadata.geometry===c.geometry&&Array.isArray(c.metadata.axes)&&c.metadata.axes.length===3,'coordinate metadata');
  for(let i=0;i<3;i++){const a=c.metadata.axes[i];need(record(a)&&a.key==='x'+(i+1)&&a.active===(i<dim)&&text(a.displayName)&&text(a.nativeName)&&(a.unit===null||text(a.unit)),'coordinate axis metadata');}
  if(record(v.state)&&record(v.state.grid))need(v.state.grid.geometry===c.geometry,'grid/data geometry');
 }else need(dim<3&&(id.caseId==='Sod'||id.caseId==='CellularDet'),'new model requires coordinate contract');
 need(Array.isArray(d.axes)&&d.axes.length===dim,'axes');
 for(let axis=0;axis<d.axes.length;axis++){
  const a=d.axes[axis];need(record(a)&&a.name==='x'+(axis+1)&&(a.unit===null||text(a.unit))&&Array.isArray(a.values)&&a.values.length===shape[shape.length-1-axis]&&a.values.every(x=>typeof x==='number'&&Number.isFinite(x)),'coordinates');
  for(let i=1;i<a.values.length;i++)need(a.values[i]>a.values[i-1],'coordinate order');
  if(record(d.coordinates)&&record(d.coordinates.metadata)&&Array.isArray(d.coordinates.metadata.axes)){const meta=d.coordinates.metadata.axes[axis];need(record(meta)&&a.unit===meta.unit,'axis metadata unit');}
 }
 need(Array.isArray(d.fields)&&d.fields.length>=1&&d.fields.length<=64,'fields');const keys=new Set();
 for(const f of d.fields){need(record(f)&&text(f.key)&&f.key.length>0&&!keys.has(f.key)&&text(f.displayName)&&(f.unit===null||text(f.unit))&&Array.isArray(f.values)&&f.values.length===s.count&&f.values.every(x=>typeof x==='number'&&Number.isFinite(x))&&typeof f.min==='number'&&Number.isFinite(f.min)&&typeof f.max==='number'&&Number.isFinite(f.max),'field');keys.add(f.key);need(f.min===Math.min(...f.values)&&f.max===Math.max(...f.values),'field extrema');}
 return v as unknown as CorePreview;
}
export function validateRealResult(v:unknown,expected:PreviewIdentity):RealPreviewResult{
 need(record(v)&&v.protocolVersion===PROTOCOL_VERSION&&record(v.identity)&&text(v.generatedAt),'envelope');
 for(const key of ['requestId','projectId','caseId','configRevision','buildId','binarySha256','profileId'] as const)need(v.identity[key]===expected[key],'envelope '+key);
 validateCorePreview(v.core,expected);return v as unknown as RealPreviewResult;
}
export function validatePreviewStatus(v:unknown,projectId:string):PreviewStatus {
 need(record(v)&&v.protocolVersion===PROTOCOL_VERSION&&v.projectId===projectId&&typeof v.ready==='boolean'&&text(v.reason,4096)&&['none','generating','succeeded','failed','cancelled'].includes(String(v.state))&&validProfile(v.profile),'status');
 need(v.error===undefined||text(v.error,65536),'error');
 if(v.requestId!==undefined)need(typeof v.requestId==='string'&&/^[a-f0-9-]{36}$/.test(v.requestId),'request ID');
 if(v.build!==undefined)need(record(v.build)&&text(v.build.buildId)&&record(v.build.outputBinary)&&record(v.build.outputBinary.fingerprint)&&text(v.build.outputBinary.fingerprint.sha256),'build');
 if(v.profiles!==undefined)need(Array.isArray(v.profiles)&&v.profiles.length<=96&&v.profiles.every(validProfile)&&new Set(v.profiles.map(p=>p.id)).size===v.profiles.length,'profiles');
 if(v.queue!==undefined){
  need(record(v.queue)&&v.queue.activeRequestId===v.requestId&&Number.isSafeInteger(v.queue.replacedPending)&&Number(v.queue.replacedPending)>=0,'queue');
  if(v.queue.pendingRequestId!==undefined)need(typeof v.queue.pendingRequestId==='string'&&/^[a-f0-9-]{36}$/.test(v.queue.pendingRequestId)&&v.queue.pendingRequestId!==v.requestId,'pending request');
 }
 if(v.session!==undefined){
  need(record(v.session)&&Number.isSafeInteger(v.session.generation)&&Number(v.session.generation)>0&&text(v.session.processToken)&&typeof v.session.stage==='string'&&['request','input','configuration','support','setup','eos','sampling','initialization','initial-refinement','source-validation','complete'].includes(v.session.stage),'session');
  for(const key of ['elapsedMilliseconds','transportParseMilliseconds'])if(v.session[key]!==undefined)need(typeof v.session[key]==='number'&&Number.isFinite(v.session[key])&&Number(v.session[key])>=0,'session timing');
  if(v.session.sequence!==undefined)need(Number.isInteger(v.session.sequence)&&Number(v.session.sequence)>=1&&Number(v.session.sequence)<=256,'session sequence');
  if(v.session.resources!==undefined)need(record(v.session.resources)&&v.session.resources.resultReused===false,'session resources');
 }
 if(v.timing!==undefined){
  need(record(v.timing),'timing');
  for(const value of Object.values(v.timing))need(typeof value==='number'&&Number.isFinite(value)&&value>=0,'duration');
 }
 if(v.modelCapabilities!==undefined)validateModelCapabilities(v.modelCapabilities);
 return v as unknown as PreviewStatus;
}

export function validateParameterExtensions(v:Record<string,unknown>){
 const m=v.parameterMetadata,b=v.graphicalBindings;
 // Unknown optional extension versions are ignored, never interpreted as v1.
 if(m!==undefined){need(record(m),'metadata object');if(m.version!=='1')delete v.parameterMetadata;}
 if(b!==undefined){need(record(b),'binding object');if(b.version!=='1')delete v.graphicalBindings;}
 if(record(m)&&m.version==='1'){
  need(m.coverage==='observed-case-setup-reads'&&m.complete===false&&Array.isArray(m.parameters)&&m.parameters.length<=128,'metadata coverage');
  const keys=new Set();
  for(const q of m.parameters){
   need(record(q)&&text(q.key)&&q.key.length>0&&!keys.has(q.key)&&['float','int','bool','string',null].includes(q.type as string|null),'parameter');keys.add(q.key);
   for(const k of ['explicitValue','effectiveValue','defaultValue'])need(q[k]===null||(q.type==='float'&&typeof q[k]==='number'&&Number.isFinite(q[k]))||(q.type==='int'&&Number.isSafeInteger(q[k]))||(q.type==='bool'&&typeof q[k]==='boolean')||(q.type==='string'&&text(q[k],65536)),'parameter value');
   need(['explicit','default','unknown'].includes(String(q.valueSource))&&(q.sourceReason===null||text(q.sourceReason))&&(q.unit===null||text(q.unit))&&(q.description===null||text(q.description,4096))&&(q.rawValue===undefined||text(q.rawValue,65536)),'parameter source');
   need(Array.isArray(q.diagnostics)&&q.diagnostics.length<=128,'parameter diagnostics');for(const d of q.diagnostics)need(record(d)&&['info','warning','error'].includes(String(d.severity))&&text(d.code)&&text(d.message,65536),'parameter diagnostic');
   if(q.constraints!==undefined)bounds(q.constraints);
  }
 }
 if(record(b)&&b.version==='1'){
  need(Array.isArray(b.items)&&b.items.length<=128,'bindings');const ids=new Set();
  for(const q of b.items){need(record(q)&&text(q.id)&&!ids.has(q.id)&&text(q.parameterKey)&&q.kind==='axis-position'&&q.axis==='x1'&&q.clamping==='none'&&q.invalidBehavior==='retain-input-and-report'&&typeof q.editable==='boolean'&&typeof q.coordinate==='number'&&Number.isFinite(q.coordinate),'binding');ids.add(q.id);bounds(q);
   need(v.status==='ok'&&q.coordinate>Number(q.min)&&q.coordinate<Number(q.max),'binding range');
   const parameter=record(m)&&Array.isArray(m.parameters)?m.parameters.find(x=>record(x)&&x.key===q.parameterKey):undefined;
   need(record(parameter)&&parameter.effectiveValue===q.coordinate&&parameter.valueSource!=='unknown','binding metadata');
   need(record(parameter.constraints)&&['min','max','minInclusive','maxInclusive'].every(k=>(parameter.constraints as Record<string,unknown>)[k]===q[k]),'binding constraints');
  }
 }
}
function bounds(v:unknown){need(record(v)&&typeof v.min==='number'&&Number.isFinite(v.min)&&typeof v.max==='number'&&Number.isFinite(v.max)&&v.min<v.max&&typeof v.minInclusive==='boolean'&&typeof v.maxInclusive==='boolean','constraints');}

function validProfile(v:unknown):boolean {
 return record(v)&&text(v.id)&&!!v.id&&text(v.caseId,128)&&!!v.caseId
  &&(v.dimension===1||v.dimension===2||v.dimension===3)
  &&text(v.buildProfileId)&&v.runnerKind==='existing-arch-cli'&&typeof v.configured==='boolean'
  &&Number.isSafeInteger(v.defaultSampleCount)&&Number(v.defaultSampleCount)>=2
  &&Number.isSafeInteger(v.maxSampleCount)&&Number(v.maxSampleCount)>=Number(v.defaultSampleCount)
  &&Number(v.maxSampleCount)<=65536;
}
export function validateModelCapabilities(v:unknown):ModelCapability[]{
 need(Array.isArray(v)&&v.length<=32,'model capabilities');const ids=new Set();
 for(const m of v){
  need(record(m)&&text(m.caseId,128)&&!!m.caseId&&!ids.has(m.caseId)&&Array.isArray(m.dimensions)&&m.dimensions.length>0&&m.dimensions.every(d=>d===1||d===2||d===3)&&new Set(m.dimensions).size===m.dimensions.length&&Array.isArray(m.geometries)&&m.geometries.length>0&&m.geometries.every(g=>['cartesian','cylindrical','spherical'].includes(String(g)))&&new Set(m.geometries).size===m.geometries.length&&m.previewBackend==='cpu','model identity');ids.add(m.caseId);
  need(Array.isArray(m.fields)&&m.fields.length>0&&m.fields.length<=64&&m.fields.every(k=>text(k)&&!!k)&&new Set(m.fields).size===m.fields.length&&Number.isInteger(m.maxFields)&&Number(m.maxFields)>=m.fields.length&&Number(m.maxFields)<=64&&Number.isInteger(m.maxResponseBytes)&&Number(m.maxResponseBytes)>0&&Number(m.maxResponseBytes)<=8388608,'model limits');
  const budgets=m.samplingByDimension;
  if(budgets!==undefined)need(Array.isArray(budgets)&&budgets.length===m.dimensions.length&&new Set(budgets.map(s=>record(s)?s.dimension:undefined)).size===budgets.length,'dimensional budgets');
  for(const dimension of m.dimensions){
   const s=Array.isArray(budgets)?budgets.find(s=>record(s)&&s.dimension===dimension):m.dimensions.length===1?m.sampling:undefined;
   const maxAxis=dimension===1?4096:dimension===2?256:64,maxTotal=dimension===1?4096:dimension===2?65536:32768;
   need(record(s)&&Array.isArray(s.defaultShape)&&s.defaultShape.length===dimension&&Number.isInteger(s.minPerAxis)&&Number(s.minPerAxis)>=2&&Number.isInteger(s.maxPerAxis)&&Number(s.maxPerAxis)>=Number(s.minPerAxis)&&Number(s.maxPerAxis)<=maxAxis&&Number.isInteger(s.maxTotalSamples)&&Number(s.maxTotalSamples)>0&&Number(s.maxTotalSamples)<=maxTotal&&s.defaultShape.every(n=>Number.isInteger(n)&&n>=Number(s.minPerAxis)&&n<=Number(s.maxPerAxis))&&s.defaultShape.reduce((a:number,b:number)=>a*b,1)<=Number(s.maxTotalSamples),'model sampling');
  }
 }
 return v as ModelCapability[];
}
