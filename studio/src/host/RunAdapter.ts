import type {FileFingerprint} from './contracts.ts';
import {PROTOCOL_VERSION} from './contracts.ts';
import {record} from './previewValidation.ts';
import {validateConfigurationInspection} from './configurationValidation.ts';
import type {PrepareRunRequest,RunPreparation,RunState,RunAcceptance} from './runContracts.ts';
const uuid=(v:unknown)=>typeof v==='string'&&/^[a-f0-9]{8}-[a-f0-9-]{27}$/.test(v);
const text=(v:unknown)=>typeof v==='string'&&v.length>0;
const fingerprint=(v:unknown):v is FileFingerprint=>record(v)&&typeof v.sha256==='string'&&/^[a-f0-9]{64}$/.test(v.sha256)&&Number.isSafeInteger(v.size)&&Number(v.size)>=0&&typeof v.modifiedTime==='string'&&Number.isFinite(Date.parse(v.modifiedTime));
function scope(v:unknown,projectId:string){if(!record(v)||v.protocolVersion!==PROTOCOL_VERSION||v.projectId!==projectId)throw new Error('Stale or incompatible Run response.');}
export function validateRunPreparation(v:unknown,expected:PrepareRunRequest&{configPath:string;binaryPath:string}):RunPreparation{
 scope(v,expected.projectId);
 if(!record(v)||!uuid(v.planId)||v.caseId!==expected.caseId||v.mode!==expected.mode||!text(v.createdAt)||
  !record(v.binary)||v.binary.relativePath!==expected.binaryPath||v.binary.sourceClaim!=='compiled-version-only'||!fingerprint(v.binary.fingerprint)||
  !record(v.config)||v.config.relativePath!==expected.configPath||!fingerprint(v.config.fingerprint)||v.config.fingerprint.sha256!==expected.configRevision||
  !Array.isArray(v.issues)||v.issues.some(i=>typeof i!=='string')||typeof v.canConfirm!=='boolean'||
  v.simulationReadiness!=='core-startup-pending'||!(v.checkpointPath===null||text(v.checkpointPath))||
  !Array.isArray(v.pendingChecks)||!v.pendingChecks.length||v.pendingChecks.some(i=>!text(i))||!Array.isArray(v.pathChecks))
  throw new Error('Run plan does not match the selected saved input and binary.');
 if(!record(v.inspection)||!record(v.inspection.identity)||!uuid(v.inspection.identity.requestId))throw new Error('Run inspection identity missing.');
 const inspection=validateConfigurationInspection(v.inspection,{caseId:expected.caseId,configRevision:expected.configRevision,requestId:String(v.inspection.identity.requestId)});
 if(v.canConfirm&&(v.issues.length||inspection.status!=='ok'||inspection.completeness.state!=='complete'||inspection.diagnostics.some(d=>d.severity==='error')))
  throw new Error('Contradictory Run readiness.');
 for(const p of v.pathChecks)if(!record(p)||!text(p.key)||!text(p.cwd)||!(p.resolvedPath===null||text(p.resolvedPath))||
  !['input-file','output-directory'].includes(String(p.role))||!['ok','error','not-set','unable-to-check'].includes(String(p.status))||typeof p.message!=='string')
  throw new Error('Malformed Run resource check.');
 return v as unknown as RunPreparation;
}
export function validateRunState(v:unknown,runId:string):RunState{
 if(!record(v)||v.runId!==runId||!['starting','running','succeeded','failed','stopped'].includes(String(v.state))||
  !Number.isInteger(v.workerPid)||Number(v.workerPid)<1||!text(v.startedAt)||
  (v.processId!==undefined&&(!Number.isInteger(v.processId)||Number(v.processId)<1))||
  (v.error!==undefined&&typeof v.error!=='string')||
  (v.exitCode!==undefined&&v.exitCode!==null&&!Number.isInteger(v.exitCode))||
  (v.signal!==undefined&&v.signal!==null&&typeof v.signal!=='string')||
  (v.finishedAt!==undefined&&!text(v.finishedAt)))throw new Error('Malformed or mismatched Run state.');
 if(['succeeded','failed','stopped'].includes(String(v.state))&&!v.finishedAt)throw new Error('Terminal Run state lacks completion identity.');
 if(v.state==='succeeded'&&v.exitCode!==0)throw new Error('Successful Run has no zero exit code.');
 return v as unknown as RunState;
}
export function validateRunAcceptance(v:unknown,projectId:string):RunAcceptance{
 scope(v,projectId);
 if(!record(v)||!uuid(v.runId)||!Number.isInteger(v.terminalPid)||Number(v.terminalPid)<1)throw new Error('Invalid terminal Run handoff.');
 validateRunState(v.state,String(v.runId));return v as unknown as RunAcceptance;
}
export function validateRunStatus(v:unknown,projectId:string,runId:string){scope(v,projectId);return validateRunState(v,runId);}
