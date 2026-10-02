import type {FileFingerprint} from './contracts.ts';
import type {ConfigurationInspection,PathCheck} from './configurationContracts.ts';
export interface PrepareRunRequest {projectId:string;caseId:string;configRevision:string;mode:'run'|'restart'}
export interface ConfirmRunRequest {projectId:string;planId:string;confirmation:'run-saved-input-with-compiled-binary'}
export interface RunPreparation {
 planId:string;projectId:string;caseId:string;mode:'run'|'restart';createdAt:string;
 binary:{relativePath:string;fingerprint:FileFingerprint;sourceClaim:'compiled-version-only'};
 config:{relativePath:string;fingerprint:FileFingerprint};
 inspection:ConfigurationInspection;pathChecks:PathCheck[];issues:string[];
 canConfirm:boolean;simulationReadiness:'core-startup-pending';
 checkpointPath:string|null;
 pendingChecks:string[];
}
export interface RunState {
 runId:string;state:'starting'|'running'|'succeeded'|'failed'|'stopped';
 workerPid:number;processId?:number;processStartTicks?:string;processIdentity?:'captured'|'exited-before-observation';
 startedAt:string;finishedAt?:string;exitCode?:number|null;signal?:string|null;error?:string;
}
export interface RunAcceptance {projectId:string;runId:string;terminalPid:number;state:RunState}
