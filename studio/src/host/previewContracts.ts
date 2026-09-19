import type {BuildManifest} from './contracts.ts';
export const PREVIEW_SCHEMA='1.0';
export const MAX_PREVIEW_BYTES=8*1024*1024;
export interface PreviewProfile { id:string; displayName:string; buildProfileId:string; caseId:'Sod'; dimension:1; defaultSampleCount:512; maxSampleCount:4096; runnerKind:'existing-arch-cli'; configured:boolean }
export interface RealPreviewRequest { projectId:string; profileId:string; configText:string; configRevision:string; requestedSampleCount?:number }
export interface PreviewIdentity { requestId:string; projectId:string; caseId:'Sod'; configRevision:string; buildId:string; binarySha256:string; profileId:string }
export interface PreviewDiagnostic { severity:'info'|'warning'|'error'; code:string; message:string }
export interface RealPreviewData { dimension:1; kind:'line'; sampling:{kind:'uniform';valueLocation:'init-sample';position:'bin-center';count:number;shape:number[];order:'x1-fastest'}; axes:{name:string;unit:string|null;values:number[]}[]; fields:{key:string;displayName:string;unit:string|null;values:number[];min:number;max:number}[] }
export interface CorePreview { schemaVersion:'1.0'; kind:'initial-state-preview'; status:'ok'|'error'; stage:string; identity:{requestId:string;caseId:string;configRevision:string}|null; execution?:{previewBackend:'cpu';simulationReadiness:'not_checked';timeStepping:'not_executed';scientificOutput:'not_created'}; data:RealPreviewData|null; diagnostics:PreviewDiagnostic[] }
export interface RealPreviewResult { protocolVersion:string; identity:PreviewIdentity; generatedAt:string; core:CorePreview }
export type PreviewRunState='none'|'generating'|'succeeded'|'failed'|'cancelled';
export interface PreviewStatus { protocolVersion:string; projectId:string; profile:PreviewProfile; ready:boolean; reason:string; state:PreviewRunState; requestId?:string; error?:string; diagnostics?:PreviewDiagnostic[]; result?:RealPreviewResult; build?:BuildManifest }
