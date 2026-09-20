import type {BuildManifest} from './contracts.ts';
export const PREVIEW_SCHEMA='1.0';
export const MAX_PREVIEW_BYTES=8*1024*1024;
export interface PreviewProfile { id:string; displayName:string; buildProfileId:string; caseId:'Sod'|'CellularDet'; dimension:1|2; defaultSampleCount:number; maxSampleCount:number; defaultShape?:number[]; maxPerAxis?:number; runnerKind:'existing-arch-cli'; configured:boolean }
export interface RealPreviewRequest { projectId:string; profileId:string; configText:string; configRevision:string; requestedSampleCount?:number; requestedShape?:number[] }
export interface PreviewIdentity { requestId:string; projectId:string; caseId:'Sod'|'CellularDet'; configRevision:string; buildId:string; binarySha256:string; profileId:string }
export interface PreviewDiagnostic { severity:'info'|'warning'|'error'; code:string; message:string }
export interface RealPreviewData { dimension:1|2; kind:'line'|'grid'; sampling:{kind:'uniform';valueLocation:'init-sample';position:'bin-center';count:number;shape:number[];order:'x1-fastest';fixedCoordinates?:{name:string;value:number;unit:string|null}[]}; axes:{name:string;unit:string|null;values:number[]}[]; fields:{key:string;displayName:string;unit:string|null;values:number[];min:number;max:number}[] }
export interface CorePreview { schemaVersion:'1.0'; kind:'initial-state-preview'; status:'ok'|'error'; stage:string; identity:{requestId:string;caseId:string;configRevision:string}|null; execution?:{previewBackend:'cpu';simulationReadiness:'not_checked';timeStepping:'not_executed';scientificOutput:'not_created'}; state?:Record<string,unknown>|null; parameterMetadata?:ParameterMetadata; graphicalBindings?:GraphicalBindings; data:RealPreviewData|null; diagnostics:PreviewDiagnostic[] }
export interface RealPreviewResult { protocolVersion:string; identity:PreviewIdentity; generatedAt:string; core:CorePreview }
export type PreviewRunState='none'|'generating'|'succeeded'|'failed'|'cancelled';
export interface PreviewStatus { protocolVersion:string; projectId:string; profile:PreviewProfile; profiles?:PreviewProfile[]; modelCapabilities?:ModelCapability[]; failure?:CorePreview; ready:boolean; reason:string; state:PreviewRunState; requestId?:string; error?:string; diagnostics?:PreviewDiagnostic[]; result?:RealPreviewResult; build?:BuildManifest }

export interface ParameterConstraints {min:number;max:number;minInclusive:boolean;maxInclusive:boolean}
export interface CoreParameter {key:string;type:'float'|'int'|'bool'|'string'|null;explicitValue:number|string|boolean|null;effectiveValue:number|string|boolean|null;defaultValue:number|string|boolean|null;rawValue?:string;valueSource:'explicit'|'default'|'unknown';sourceReason:string|null;unit:string|null;description:string|null;constraints?:ParameterConstraints;diagnostics:PreviewDiagnostic[]}
export interface CoreBinding extends ParameterConstraints {id:string;parameterKey:string;kind:'axis-position';axis:'x1';coordinate:number;clamping:'none';invalidBehavior:'retain-input-and-report';editable:boolean}
export interface ParameterMetadata {version:'1';coverage:'observed-case-setup-reads';complete:false;parameters:CoreParameter[]}
export interface GraphicalBindings {version:'1';items:CoreBinding[]}

export interface ModelCapability {caseId:string;dimensions:number[];geometries:string[];previewBackend:'cpu';fields:string[];maxFields:number;maxResponseBytes:number;sampling:{defaultShape:number[];minPerAxis:number;maxPerAxis:number;maxTotalSamples:number};supportedShockDirections?:number[]}
