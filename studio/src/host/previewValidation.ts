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
 const d=v.data;const two=id.caseId==='CellularDet';need(record(d)&&d.dimension===(two?2:1)&&d.kind===(two?'grid':'line'),'dimension/kind');
 const s=d.sampling;need(record(s)&&s.kind==='uniform'&&s.valueLocation==='init-sample'&&s.position==='bin-center'&&s.order==='x1-fastest'&&Number.isInteger(s.count)&&Number(s.count)>=2&&Number(s.count)<=(two?65536:4096)&&Array.isArray(s.shape)&&s.shape.length===(two?2:1),'sampling');
 const shape=s.shape as number[];need(shape.every(n=>Number.isInteger(n)&&n>=2&&n<=(two?256:4096))&&shape.reduce((a,b)=>a*b,1)===s.count,'sampling shape');
 if(count!==undefined)need(Array.isArray(count)?JSON.stringify(count)===JSON.stringify(shape):count===s.count,'requested sampling');
 if(two)need(Array.isArray(s.fixedCoordinates)&&s.fixedCoordinates.length===1&&record(s.fixedCoordinates[0])&&s.fixedCoordinates[0].name==='x3'&&s.fixedCoordinates[0].value===0&&(s.fixedCoordinates[0].unit===null||text(s.fixedCoordinates[0].unit)),'fixed coordinates');
 need(Array.isArray(d.axes)&&d.axes.length===(two?2:1),'axes');
 for(let axis=0;axis<d.axes.length;axis++){
  const a=d.axes[axis];need(record(a)&&a.name==='x'+(axis+1)&&(a.unit===null||text(a.unit))&&Array.isArray(a.values)&&a.values.length===shape[shape.length-1-axis]&&a.values.every(x=>typeof x==='number'&&Number.isFinite(x)),'coordinates');
  for(let i=1;i<a.values.length;i++)need(a.values[i]>a.values[i-1],'coordinate order');
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
 need(record(v)&&v.protocolVersion===PROTOCOL_VERSION&&v.projectId===projectId&&typeof v.ready==='boolean'&&text(v.reason,4096)&&['none','generating','succeeded','failed','cancelled'].includes(String(v.state))&&record(v.profile)&&((v.profile.id==='sod-initial-cpu'&&v.profile.caseId==='Sod')||(v.profile.id==='cellular-initial-cpu'&&v.profile.caseId==='CellularDet')),'status');
 need(v.error===undefined||text(v.error,65536),'error');
 if(v.requestId!==undefined)need(typeof v.requestId==='string'&&/^[a-f0-9-]{36}$/.test(v.requestId),'request ID');
 if(v.build!==undefined)need(record(v.build)&&text(v.build.buildId)&&record(v.build.outputBinary)&&record(v.build.outputBinary.fingerprint)&&text(v.build.outputBinary.fingerprint.sha256),'build');
 if(v.profiles!==undefined)need(Array.isArray(v.profiles)&&v.profiles.length<=2&&v.profiles.every(p=>record(p)&&((p.id==='sod-initial-cpu'&&p.caseId==='Sod'&&p.dimension===1)||(p.id==='cellular-initial-cpu'&&p.caseId==='CellularDet'&&p.dimension===2))),'profiles');
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

export function validateModelCapabilities(v:unknown):ModelCapability[]{
 need(Array.isArray(v)&&v.length<=32,'model capabilities');const ids=new Set();
 for(const m of v){need(record(m)&&text(m.caseId)&&!ids.has(m.caseId)&&Array.isArray(m.dimensions)&&m.dimensions.every(d=>d===1||d===2)&&Array.isArray(m.geometries)&&m.geometries.includes('cartesian')&&m.previewBackend==='cpu','model identity');ids.add(m.caseId);
 need(Array.isArray(m.fields)&&m.fields.length>0&&m.fields.length<=64&&m.fields.every(k=>text(k))&&new Set(m.fields).size===m.fields.length&&Number.isInteger(m.maxFields)&&Number(m.maxFields)>=m.fields.length&&Number(m.maxFields)<=64&&Number.isInteger(m.maxResponseBytes)&&Number(m.maxResponseBytes)>0&&Number(m.maxResponseBytes)<=8388608,'model limits');
 const s=m.sampling;need(record(s)&&Array.isArray(s.defaultShape)&&s.defaultShape.length>=1&&s.defaultShape.length<=2&&Number.isInteger(s.minPerAxis)&&Number(s.minPerAxis)>=2&&Number.isInteger(s.maxPerAxis)&&Number(s.maxPerAxis)>=Number(s.minPerAxis)&&Number(s.maxPerAxis)<=4096&&Number.isInteger(s.maxTotalSamples)&&Number(s.maxTotalSamples)<=65536&&s.defaultShape.every(n=>Number.isInteger(n)&&n>=Number(s.minPerAxis)&&n<=Number(s.maxPerAxis))&&s.defaultShape.reduce((a:number,b:number)=>a*b,1)<=Number(s.maxTotalSamples),'model sampling');
 }
 return v as ModelCapability[];
}
