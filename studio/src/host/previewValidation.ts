import {PROTOCOL_VERSION} from './contracts.ts';
import type {CorePreview,PreviewIdentity,RealPreviewResult,PreviewStatus} from './previewContracts.ts';
export function record(v:unknown):v is Record<string,unknown>{return !!v&&typeof v==='object'&&!Array.isArray(v);}
function need(ok:unknown,message:string):asserts ok {if(!ok)throw new Error('Invalid Preview response: '+message);}
function text(v:unknown,max=256):v is string{return typeof v==='string'&&v.length<=max;}
export function validateCorePreview(v:unknown,id:Pick<PreviewIdentity,'requestId'|'caseId'|'configRevision'>,count?:number):CorePreview {
 need(record(v)&&v.schemaVersion==='1.0'&&v.kind==='initial-state-preview'&&['ok','error'].includes(String(v.status))&&text(v.stage),'schema/status');
 need(Array.isArray(v.diagnostics)&&v.diagnostics.length<=128,'diagnostics');
 for(const d of v.diagnostics)need(record(d)&&['info','warning','error'].includes(String(d.severity))&&text(d.code)&&text(d.message,65536),'diagnostic');
 if(v.status==='error'&&v.stage==='input'&&v.identity===null){need(v.data===null,'error data');return v as unknown as CorePreview;}
 need(record(v.identity)&&v.identity.requestId===id.requestId&&v.identity.caseId===id.caseId&&v.identity.configRevision===id.configRevision,'identity');
 need(record(v.execution)&&v.execution.previewBackend==='cpu'&&v.execution.timeStepping==='not_executed'&&v.execution.scientificOutput==='not_created'&&v.execution.simulationReadiness==='not_checked','execution');
 if(v.status==='error'){need(v.data===null,'error data');return v as unknown as CorePreview;}
 const d=v.data;need(record(d)&&d.dimension===1&&d.kind==='line','dimension/kind');
 const s=d.sampling;need(record(s)&&s.kind==='uniform'&&s.valueLocation==='init-sample'&&s.position==='bin-center'&&s.order==='x1-fastest'&&Number.isInteger(s.count)&&Number(s.count)>=2&&Number(s.count)<=4096&&(count===undefined||s.count===count)&&Array.isArray(s.shape)&&s.shape.length===1&&s.shape[0]===s.count,'sampling');
 need(Array.isArray(d.axes)&&d.axes.length===1&&record(d.axes[0]),'axes');
 const a=d.axes[0];need(text(a.name)&&a.name.length>0&&(a.unit===null||text(a.unit))&&Array.isArray(a.values)&&a.values.length===s.count&&a.values.every(x=>typeof x==='number'&&Number.isFinite(x)),'coordinates');
 for(let i=1;i<a.values.length;i++)need(a.values[i]>a.values[i-1],'coordinate order');
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
 need(record(v)&&v.protocolVersion===PROTOCOL_VERSION&&v.projectId===projectId&&typeof v.ready==='boolean'&&text(v.reason,4096)&&['none','generating','succeeded','failed','cancelled'].includes(String(v.state))&&record(v.profile)&&v.profile.id==='sod-initial-cpu'&&v.profile.caseId==='Sod','status');
 need(v.error===undefined||text(v.error,65536),'error');
 if(v.requestId!==undefined)need(typeof v.requestId==='string'&&/^[a-f0-9-]{36}$/.test(v.requestId),'request ID');
 if(v.build!==undefined)need(record(v.build)&&text(v.build.buildId)&&record(v.build.outputBinary)&&record(v.build.outputBinary.fingerprint)&&text(v.build.outputBinary.fingerprint.sha256),'build');
 return v as unknown as PreviewStatus;
}
