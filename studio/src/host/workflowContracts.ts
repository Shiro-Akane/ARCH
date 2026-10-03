import type {ConfigurationBuildScope,ConfigurationIdentity,CoordinateSystem} from './configurationContracts';
export type WorkflowOperation='inspect-case'|'amr-resources'|'preview-amr';
export interface RegisteredCase {
 caseId:string;initialFieldPreview:boolean;initialAmrPreview:boolean;previewDimensions:number[];
 inspection:{registered:boolean;setupReads:boolean;primitiveSinkProbe:boolean;sourceFile:string|null;compiledSourceSha256:string|null;reviewedUnitEvidence:string;coverage:string;hostWallTimeoutSeconds:number;automaticExpressionInference:false};
}
export interface DiscoveryResponse extends ConfigurationBuildScope {
 protocolVersion:string;cases:RegisteredCase[];
 fieldModels:{caseId:string;dimensions:number[];geometries:string[]}[];
 amr:{cases:string[];geometries:string[];defaultMaxBlocks:number;defaultMemoryMiB:number}|null;
}
export interface ResourceLevel {level:number;fullDomainLeafBlocks:number|null;activeCells:number|null;baseStateBytes:number|null;stateBytesIncludingSpecies:number|null;overflow:boolean}
export interface ResourceEstimate {version:'1';dimension:number;levels:ResourceLevel[];poolPreallocatedBaseBytes:number|null;configuredPoolCapacity:number;advisoryOnly:true;assumption:string;scope:string;excludes:string[];oomPrediction:'not-provided';speciesCount:number|null}
export interface AmrLeaf {logicalKey:string;level:number;logicalIndex:number[];lower:number[];upper:number[];cellShape:number[];cellSpacing:number[]}
export interface AmrMesh {
 version:'1';kind:'amr-leaf-mesh';dimension:1|2|3;geometry:'cartesian'|'spherical'|'cylindrical';unit:string|null;
 coordinates?:{version:'1';basis:'native-grid';metadata:CoordinateSystem};
 leaves:AmrLeaf[];leafCount:number;levelCounts:{level:number;leafBlocks:number}[];
 complete:boolean;completedPasses:number;snapshot:'none'|'last-completed-balanced-hierarchy';limitedReason:string|null;
 configuredMaxBlocks:number;workingCapacity:number;resources:ResourceEstimate;
}
export interface WorkflowCore {
 schemaVersion:'1.0';kind:'case-inspection'|'amr-resource-estimate'|'initial-amr-preview';status:'ok'|'limited'|'error';
 identity:{requestId:string;caseId:string;configRevision:string};
 diagnostics:{severity:string;code:string;message:string}[];execution:Record<string,unknown>;
 state?:Record<string,unknown>|null;data:AmrMesh|ResourceEstimate|CaseProbe|null;
 parameterMetadata?:{version:'1';complete:false;automaticExpressionInference:false;coverage:string;parameters:ObservedParameter[];unobservedInputKeys:string[];unobservedMeaning:string};
 capability?:RegisteredCase['inspection'];
}
export interface ObservedParameter {
 key:string;type:string|null;defaultValue:unknown;explicitValue:unknown;effectiveValue:unknown;rawValue:string|null;
 valueSource:string;sourceReason:string|null;unit:string|null;description:string|null;readCount:number;
 unitEvidence:Record<string,unknown>;diagnostics:unknown[];
}
export interface CaseProbe {
 kind:'initial-primitive-probe';completeFieldCoverage:false;sampleCount:number;valueLocation:string;sampling:string;velocityBasis:string;
 samples:{cartesianPosition:number[];positionUnit:string;thermodynamicInput:string;fields:{key:string;unit:string|null;value:number;consumedByConversion:boolean}[];massFractions:number[];massFractionUnit:string}[];
}
export interface WorkflowRequest {projectId:string;caseId:string;configText:string;configRevision:string;operation:WorkflowOperation;meshMaxBlocks?:number;meshMemoryMiB?:number}
export interface WorkflowResult {identity:ConfigurationIdentity;operation:WorkflowOperation;core:WorkflowCore}
export interface WorkflowStatus {protocolVersion:string;projectId:string;state:'none'|'running'|'succeeded'|'failed'|'cancelled';requestId?:string;operation?:WorkflowOperation;stage?:string;error?:string;result?:WorkflowResult;failure?:WorkflowCore}
