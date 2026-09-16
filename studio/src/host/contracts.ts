export const PROTOCOL_VERSION = '1.0';
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
