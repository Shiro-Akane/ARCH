export const PROTOCOL_VERSION = '1.2';
export interface HostCapabilities { readProject: boolean; writeConfig: boolean; build: boolean; preview: boolean; watchFiles: boolean }
export interface HostInfo { protocolVersion: string; hostKind: 'local'; platform: string; projectRoot: string; capabilities: HostCapabilities }
export type SourceState = 'missing' | 'available' | 'changed' | 'unknown';
export type ConfigFileState = 'missing' | 'available' | 'changed-externally' | 'unknown';
export type BinaryState = 'missing' | 'available' | 'unknown';
export interface ProjectFileRef { relativePath: string; kind: 'case-source' | 'parameter' | 'executable'; exists: boolean; size?: number; modifiedTime?: string; sha256?: string; changed: boolean; error?: string }
export interface ProjectSession { projectId: string; displayName: string; projectRoot: string; caseSource?: ProjectFileRef; parameterFile?: ProjectFileRef; executable?: ProjectFileRef; sourceState: SourceState; configFileState: ConfigFileState; binaryState: BinaryState; mapping: 'unknown'; metadata: 'unavailable'; openedAt: string; refreshedAt: string }
export interface ProjectSnapshot { host: HostInfo; session: ProjectSession }
export interface LocalHostAdapter { connect(): Promise<ProjectSnapshot>; refresh(): Promise<ProjectSnapshot> }
// Versioned declarations only: no execution or parameter inference in Phase 2.
export interface PreviewRequest { projectId: string; caseId: string; configText: string; configRevision: string; requestedFields?: string[] }
export interface PreviewEnvelope { requestId: string; configRevision: string; buildId?: string; data?: unknown }
export interface ParameterMetadata { key: string; valueType?: 'int' | 'float' | 'bool' | 'enum' | 'string'; defaultValue?: unknown; source?: 'explicit' | 'default' | 'unknown'; unit?: string | null; description?: string | null; min?: number; max?: number; enumValues?: string[] }
export interface ParameterBinding { parameterKey: string; kind: 'axis-position' | 'radius' | 'unknown'; axis?: 'x' | 'y' | 'z'; min?: number; max?: number }

// Phase 2B: text is an opaque UTF-8 config payload, never a command or metadata source.
export interface FileFingerprint { sha256:string; size:number; modifiedTime:string }
export interface ConfigAssociation { projectId:string; relativePath:string; loadedFingerprint:FileFingerprint }
export type DiskState = 'in-sync'|'changed-externally'|'missing'|'read-error'|'unknown';
export interface ConfigLifecycleState { association?:ConfigAssociation; loadedFingerprint?:FileFingerprint; savedFingerprint?:FileFingerprint; diskState:DiskState }
export interface ConfigReadResponse { projectId:string; relativePath:string; text:string; fingerprint:FileFingerprint }
export interface SaveConfigRequest { projectId:string; relativePath:string; expectedFingerprint:FileFingerprint; text:string }
export interface SaveConfigAsRequest { projectId:string; destinationRelativePath:string; text:string }
export type ConfigFileErrorCode = 'not-found'|'permission-denied'|'outside-project-root'|'changed-externally'|'destination-exists'|'invalid-path'|'payload-too-large'|'write-failed'|'rename-failed'|'protocol-error'|'read-error';
export interface ConfigFileError { code:ConfigFileErrorCode; message:string; relativePath?:string; actual?:FileFingerprint }
export interface ConfigWriteResponse extends ConfigReadResponse { project:ProjectSnapshot }

// Phase 2C: browser requests identify only a Host-owned profile.
export type BuildState = 'not-configured'|'ready'|'queued'|'building'|'succeeded'|'failed'|'cancelled'|'unknown';
export type BinaryBuildState = 'missing'|'available'|'built-from-current-tracked-inputs'|'needs-build'|'freshness-unknown';
export interface BuildProfile {
 id:string; displayName:string; managedSourceRoot:string; buildDirRelative:string; target:string; outputBinaryRelative:string;
 caseId?:string; sourceRelativePath?:string; parallelism:number; trackedInputs:string[]; dependenciesComplete:boolean;
}
export interface BuildRequest { projectId:string; profileId:string }
export interface BuildEvent { projectId:string; buildId:string; sequence:number; timestamp:string; kind:'state'|'stdout'|'stderr'; state?:BuildState; text?:string }
export interface BuildResult { buildId:string; projectId:string; state:'succeeded'|'failed'|'cancelled'; exitCode?:number|null; signal?:string|null; startedAt:string; finishedAt:string; error?:string }
export interface InputFingerprint { relativePath:string; fingerprint:FileFingerprint }
export interface BuildManifest {
 manifestVersion:'1'; buildId:string; projectId:string; profileId:string; caseId?:string; managedSourceRoot:string;
 sourceGitHead?:string; repositoryDirty?:boolean; buildProfileFingerprint:string; sourceFingerprint?:FileFingerprint;
 trackedInputFingerprints:InputFingerprint[]; preBuildInputFingerprints:InputFingerprint[]; inputsStableDuringBuild:boolean;
 buildDirectory:string; target:string; startedAt:string; finishedAt:string;
 outputBinary:{relativePath:string; absolutePath:string; fingerprint:FileFingerprint}; preBuildBinaryFingerprint?:FileFingerprint;
}
export interface BuildSnapshot {
 protocolVersion:string; projectId:string; profile?:BuildProfile; configured:boolean; reason?:string; state:BuildState;
 mappingState:'configured'|'unknown'; binaryState:BinaryBuildState; freshnessReason:string; changedInputs:string[];
 lastSuccessfulBuild?:BuildManifest; latestResult?:BuildResult; activeBuildId?:string;
}
export interface BuildEvents { protocolVersion:string; projectId:string; buildId:string; events:BuildEvent[]; truncated:boolean; lastSequence:number }
export interface SourceResponse { protocolVersion:string; projectId:string; relativePath:string; text:string }
