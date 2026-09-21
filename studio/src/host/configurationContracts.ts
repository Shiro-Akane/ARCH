export type ConfigurationCase = 'Sod' | 'CellularDet';
export interface ConfigurationIdentity {
 projectId:string; caseId:ConfigurationCase; configRevision:string;
 requestId:string; buildId:string; binarySha256:string;
}
export interface ConfigurationRequest {projectId:string;caseId:ConfigurationCase;configText:string;configRevision:string}
export interface StandardParameter {
 key:string; type:'int'|'float'|'bool'|'string'|'expression'; group:string;
 defaultValue:number|string|boolean; defaultSource:string; aliasOf?:string;
 constraints:Record<string,unknown>; options:Record<string,unknown>|null;
 units:Record<string,unknown>; path:Record<string,unknown>|null; applicability:string;
}
export interface ConfigurationSchema {
 schemaVersion:'1.0';version:'1';kind:'configuration-schema';status:'ok';
 standardParametersComplete:true;customParametersComplete:false;constraintsComplete:false;
 parameters:StandardParameter[]; coordinateSystems?:CoordinateSystem[];
}
export function sameConfigurationIdentity(a:ConfigurationIdentity,b:ConfigurationIdentity):boolean {
 return a.projectId===b.projectId&&a.caseId===b.caseId&&a.configRevision===b.configRevision
  &&a.requestId===b.requestId&&a.buildId===b.buildId&&a.binarySha256===b.binarySha256;
}

export interface ConfigurationBuildScope {projectId:string;buildId:string;binarySha256:string}
export interface InspectionParameter {key:string;parsedValue:number|string|boolean;defaultValue:number|string|boolean;rawValue:string|null;valueSource:'explicit'|'default'|'alias';sourceKey?:string;valueStage:'typed-input-before-setup-and-policy-resolution';applicable?:boolean;units?:Record<string,unknown>}
export interface ConfigurationInspection {schemaVersion:'1.0';version:'1';kind:'configuration-inspection';status:'ok'|'error';identity:{requestId:string;caseId:string;configRevision:string};parameters:InspectionParameter[];diagnostics:{severity:'info'|'warning'|'error';code:string;message:string;parameterKey:string|null}[];execution:Record<string,string>;coordinates?:CoordinateSystem;unitSystem?:string}
export interface SchemaResponse extends ConfigurationBuildScope {protocolVersion:string;core:ConfigurationSchema}
export interface InspectionResponse {protocolVersion:string;identity:ConfigurationIdentity;core:ConfigurationInspection;pathChecks?:PathCheck[]}
export function sameBuildScope(a:ConfigurationBuildScope|null|undefined,b:ConfigurationBuildScope|null|undefined){return !!a&&!!b&&a.projectId===b.projectId&&a.buildId===b.buildId&&a.binarySha256===b.binarySha256;}

export interface CoordinateAxis {key:string;displayName:string;nativeName:string;active:boolean;kind:string;unit:string|null;blocks:number;blocksKey:string;minKey:string;maxKey:string;lowerBoundaryKey:string;upperBoundaryKey:string}
export interface CoordinateSystem {geometry:string;dimension:number;unitSystem:string;axes:CoordinateAxis[]}
export interface PathCheck {key:string;role:'input-file'|'output-directory';cwd:string;resolvedPath:string|null;status:'ok'|'error'|'not-set'|'unable-to-check';message:string;exists?:boolean;regularFile?:boolean;readable?:boolean;parent?:string;writable?:boolean}
