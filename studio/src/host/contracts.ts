export const PROTOCOL_VERSION = '1.1';
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
export interface BuildRequest { projectId: string; caseId: string }
export interface BuildEvent { requestId: string; state: 'queued' | 'running' | 'succeeded' | 'failed' | 'cancelled'; stream?: 'stdout' | 'stderr'; text?: string }
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
