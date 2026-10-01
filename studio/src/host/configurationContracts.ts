/** Registered name is validated against the selected binary by Host discovery. */
export type ConfigurationCase = string;
export interface ConfigurationIdentity {
 projectId:string; caseId:ConfigurationCase; configRevision:string;
 requestId:string; buildId:string; binarySha256:string;
}
export interface ConfigurationRequest {projectId:string;caseId:ConfigurationCase;configText:string;configRevision:string}
export type ConfigurationScalar = number|string|boolean;
export interface ConfigurationCondition {id:string;dependencies:string[];description:string}
export interface ConditionState {conditionId:string;state:'satisfied'|'not-applicable'|'unknown-dependency';missingDependencies:string[]}
export interface SourceLocation {source:string;line:number;column:number;endColumn:number;rawValue:string|null}
export interface StandardParameter {
 presentation?:{displayName:string;description:string;subgroup:string;enabledBy?:string;toggle?:{enabledWhen:'value > 0';offValue:number;enabledValueRequired:true;preserveUneditedInput:true}};
 key:string; type:'int'|'float'|'bool'|'string'|'expression'; group:string;
 caseId:string|null;usage:'simulation'|'verification';aliasOf?:never;
 allowedDefault:{value:ConfigurationScalar;source:'documented-default';evidence:string}|null;
 templateRecommendations:{value:ConfigurationScalar;evidence:string}[];
 requirement:{kind:'required'|'conditional'|'optional';condition:ConfigurationCondition};
 constraints:Record<string,unknown>; options:Record<string,unknown>|null;
 units:Record<string,unknown>; path:Record<string,unknown>|null; applicability:ConfigurationCondition;
}
export interface ConfigurationSchema {
 schemaVersion:'1.0';version:'3';kind:'configuration-schema';status:'ok';
 standardParametersComplete:true;constraintsComplete:false;
 auxiliaryParameters:StandardParameter[];retiredKeys:string[];
 caseDeclarationsComplete:boolean;
 caseDeclarations:{caseId:string;source:string;sourceSha256:string;parameters:StandardParameter[];declarationsComplete:boolean;composition:{status:string;inspectionRequired:boolean}}[];
 parameters:StandardParameter[]; coordinateSystems?:CoordinateSystem[];
}
export function sameConfigurationIdentity(a:ConfigurationIdentity,b:ConfigurationIdentity):boolean {
 return a.projectId===b.projectId&&a.caseId===b.caseId&&a.configRevision===b.configRevision
  &&a.requestId===b.requestId&&a.buildId===b.buildId&&a.binarySha256===b.binarySha256;
}

export interface ConfigurationBuildScope {projectId:string;buildId:string;binarySha256:string}
export interface InspectionParameter {
 key:string;caseId:string|null;type:StandardParameter['type'];group:string;usage:'simulation'|'verification';
 inputState:'missing'|'present'|'invalid'|'duplicate';rawValue:string|null;locations:SourceLocation[];
 parsedValue:ConfigurationScalar|null;resolvedValue:ConfigurationScalar|null;
 valueSource:'input'|'case-defined'|'derived'|'documented-default'|null;
 sourceEvidence:{owner:string;dependencies:string[]}|null;
 valueStage:'configuration-resolution-before-setup';
 requirement:ConditionState&{required:boolean|null};applicability:ConditionState;
 units:Record<string,unknown>;path:Record<string,unknown>|null;
}
export interface ConfigurationDiagnostic {
 severity:'info'|'warning'|'error';code:string;message:string;parameterKey:string|null;
 module:string|null;conditionId:string|null;
 expected:{type:string|null;units:Record<string,unknown>|null};
 locations:SourceLocation[];relatedKeys:string[];
}
export interface DiffusionInspection {version:string;enabled:boolean;modeEditable:false;source:string;sourceScope:string;forbiddenExplicitKeys:string[];channels:{coefficientKey:string;toggleKey:string;constantInputAllowed:boolean;unit:string}[]}
export interface ConfigurationInspection {diffusion?:DiffusionInspection;amrIndicators?:{choices:{value:string;available:boolean;selected:boolean;reason:string|null}[];speciesResolution:string};schemaVersion:'1.0';version:'3';kind:'configuration-inspection';status:'ok'|'error';identity:{requestId:string;caseId:string;configRevision:string};parameters:InspectionParameter[];diagnostics:ConfigurationDiagnostic[];
 coverage:{standardParametersComplete:boolean;auxiliaryParametersComplete:boolean;caseParametersComplete:boolean;conditionsComplete:boolean;diagnosticsComplete:boolean};
 completeness:{state:'complete'|'incomplete'|'invalid'|'undetermined';scope:'declared-configuration-before-setup'};execution:Record<string,string>;coordinates?:CoordinateSystem;unitSystem?:string}
export interface SchemaResponse extends ConfigurationBuildScope {protocolVersion:string;core:ConfigurationSchema}
export interface InspectionResponse {protocolVersion:string;identity:ConfigurationIdentity;core:ConfigurationInspection;pathChecks?:PathCheck[]}
export function sameBuildScope(a:ConfigurationBuildScope|null|undefined,b:ConfigurationBuildScope|null|undefined){return !!a&&!!b&&a.projectId===b.projectId&&a.buildId===b.buildId&&a.binarySha256===b.binarySha256;}

export interface CoordinateAxis {key:string;displayName:string;nativeName:string;active:boolean;kind:string;unit:string|null;blocks:number;blocksKey:string;minKey:string;maxKey:string;lowerBoundaryKey:string;upperBoundaryKey:string}
export interface CoordinateSystem {geometry:string;dimension:number;unitSystem:string;axes:CoordinateAxis[]}
export interface PathCheck {key:string;role:'input-file'|'output-directory';cwd:string;resolvedPath:string|null;status:'ok'|'error'|'not-set'|'unable-to-check';message:string;exists?:boolean;regularFile?:boolean;readable?:boolean;parent?:string;writable?:boolean}
