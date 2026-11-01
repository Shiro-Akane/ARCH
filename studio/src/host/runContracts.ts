/**
 * Define preparation, explicit confirmation and observed Run/Restart states.
 * Identity binds a proposal to saved input and compiled binary. These message
 * types confer no shell access and do not turn an unknown outcome into success.
 */
import type {FileFingerprint} from './contracts.ts';
import type {ConfigurationInspection,PathCheck} from './configurationContracts.ts';
export interface PrepareRunRequest {projectId:string;caseId:string;configRevision:string;mode:'run'|'restart'}
export interface ConfirmRunRequest {projectId:string;planId:string;confirmation:'run-saved-input-with-compiled-binary'}
export interface ExecutionPreflight {
 schemaVersion:'1.0';version:'1';kind:'execution-preflight';status:'ok'|'error';
 identity:{caseId:string;configRevision:string;requestId:string};
 execution:{meshConstruction:'not_executed';timeStepping:'not_executed'};
 data:{requestedBackend:'cpu'|'cuda'|'auto';resolvedBackend:'cpu'|'cuda';linearSolver:'none'|'denselu'|'sparseklu'|'cudss';fallbackReason:string}|null;
 diagnostics:{severity:'info'|'warning'|'error';code:string;message:string}[];
}
export interface RunPreparation {
 planId:string;projectId:string;caseId:string;mode:'run'|'restart';createdAt:string;
 binary:{relativePath:string;fingerprint:FileFingerprint;sourceClaim:'compiled-version-only'};
 config:{relativePath:string;fingerprint:FileFingerprint};
 inspection:ConfigurationInspection;pathChecks:PathCheck[];issues:string[];
 backend:ExecutionPreflight|null;
 canConfirm:boolean;simulationReadiness:'core-startup-pending';
 checkpointPath:string|null;
 pendingChecks:string[];
}
/** Windows process identity from one opened handle; a PID alone is never ownership. */
export interface WindowsRunProcessIdentity {version:'windows-v1';pid:number;creationTime:string}
export interface RunState {
 runId:string;state:'starting'|'running'|'succeeded'|'failed'|'stopped';
 workerPid:number;workerStartTicks?:string;bootId?:string;processId?:number;processStartTicks?:string;processIdentity?:'captured'|'exited-before-observation';
 windowsWorker?:WindowsRunProcessIdentity;windowsHelper?:WindowsRunProcessIdentity;
 startedAt:string;finishedAt?:string;exitCode?:number|null;signal?:string|null;error?:string;
}
export interface RunAcceptance {projectId:string;runId:string;terminalPid:number;state:RunState}
