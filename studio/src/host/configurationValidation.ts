import {PROTOCOL_VERSION} from './contracts.ts';
import {record} from './previewValidation.ts';
import type {ConfigurationBuildScope,ConfigurationInspection,ConfigurationSchema,ConfigurationRequest,InspectionResponse,SchemaResponse} from './configurationContracts.ts';
import {sameBuildScope} from './configurationContracts.ts';
const scalar=(v:unknown)=>typeof v==='string'||typeof v==='boolean'||typeof v==='number'&&Number.isFinite(v);
const strings=(v:Record<string,unknown>,keys:string[])=>keys.every(k=>typeof v[k]==='string');
export function validateConfigurationSchema(v:unknown):ConfigurationSchema {
 if(!record(v)||v.schemaVersion!=='1.0'||v.version!=='1'||v.kind!=='configuration-schema'||v.status!=='ok'||v.standardParametersComplete!==true||v.customParametersComplete!==false||v.constraintsComplete!==false||!Array.isArray(v.parameters)||v.parameters.length!==90)throw new Error('Unsupported Core configuration schema.');
 const keys=new Set<string>();
 for(const p of v.parameters){
  if(!record(p)||!strings(p,['key','type','group','defaultSource','applicability'])||!/^[A-Za-z_][A-Za-z0-9_]*$/.test(p.key as string)||keys.has(p.key as string)||!['int','float','bool','string','expression'].includes(p.type as string)||!scalar(p.defaultValue)||!record(p.constraints)||!record(p.units)||(p.options!==null&&!record(p.options))||(p.path!==null&&!record(p.path))||(p.aliasOf!==undefined&&typeof p.aliasOf!=='string'))throw new Error('Malformed standard parameter schema.');
  for(const key of ['min','max','storageMin','storageMax'])if(p.constraints[key]!==undefined&&(typeof p.constraints[key]!=='number'||!Number.isFinite(p.constraints[key])))throw new Error('Malformed parameter constraint.');
  validateUnit(p.units);
  if(p.path!==null&&(!record(p.path)||p.path.checkOwner!=='local-host'||p.path.relativeTo!=='process-working-directory'||!['input-file','output-directory'].includes(String(p.path.role))))throw new Error('Invalid Core path authority.');
  keys.add(p.key as string);
 }
 if(v.coordinateSystems!==undefined){if(!Array.isArray(v.coordinateSystems)||v.coordinateSystems.length!==9)throw new Error('Invalid coordinate catalog.');v.coordinateSystems.forEach(validateCoordinates);}
 return v as unknown as ConfigurationSchema;
}
export function validateConfigurationInspection(v:unknown,expected:{caseId:string;configRevision:string;requestId:string}):ConfigurationInspection {
 if(!record(v)||v.schemaVersion!=='1.0'||v.version!=='1'||v.kind!=='configuration-inspection'||!['ok','error'].includes(String(v.status))||!record(v.identity)||!Object.entries(expected).every(([k,x])=>v.identity&&record(v.identity)&&v.identity[k]===x)||!record(v.execution)||v.execution.setup!=='not_executed'||v.execution.simulationReadiness!=='not_checked'||v.execution.eos!=='not_loaded'||v.execution.filesystem!=='not_accessed'||v.execution.cuda!=='not_initialized'||!Array.isArray(v.parameters)||v.parameters.length>90||!Array.isArray(v.diagnostics))throw new Error('Invalid Core inspection contract or identity.');
 const keys=new Set<string>();
 for(const p of v.parameters){if(!record(p)||typeof p.key!=='string'||keys.has(p.key)||!scalar(p.parsedValue)||!scalar(p.defaultValue)||(p.rawValue!==null&&typeof p.rawValue!=='string')||!['explicit','default','alias'].includes(String(p.valueSource))||p.valueStage!=='typed-input-before-setup-and-policy-resolution'||(p.sourceKey!==undefined&&typeof p.sourceKey!=='string')||(p.applicable!==undefined&&typeof p.applicable!=='boolean')||(p.units!==undefined&&!record(p.units)))throw new Error('Malformed parsed parameter.');if(p.units!==undefined)validateUnit(p.units);keys.add(p.key);}
 for(const d of v.diagnostics)if(!record(d)||!strings(d,['severity','code','message'])||!['info','warning','error'].includes(d.severity as string)||(d.parameterKey!==null&&typeof d.parameterKey!=='string'))throw new Error('Malformed configuration diagnostic.');
 if(v.coordinates!==undefined&&v.coordinates!==null)validateCoordinates(v.coordinates);
 return v as unknown as ConfigurationInspection;
}
export function validateSchemaResponse(v:unknown,scope:ConfigurationBuildScope):SchemaResponse {
 if(!record(v)||v.protocolVersion!==PROTOCOL_VERSION||!sameBuildScope(v as unknown as ConfigurationBuildScope,scope))throw new Error('Schema build identity mismatch.');
 return {...scope,protocolVersion:PROTOCOL_VERSION,core:validateConfigurationSchema(v.core)};
}
export function validateInspectionResponse(v:unknown,request:ConfigurationRequest,scope:ConfigurationBuildScope):InspectionResponse {
 if(!record(v)||v.protocolVersion!==PROTOCOL_VERSION||!record(v.identity)||!sameBuildScope(v.identity as unknown as ConfigurationBuildScope,scope)||v.identity.caseId!==request.caseId||v.identity.configRevision!==request.configRevision||typeof v.identity.requestId!=='string'||!/^[a-f0-9-]{36}$/.test(v.identity.requestId))throw new Error('Inspection identity mismatch.');
 const core=validateConfigurationInspection(v.core,{caseId:request.caseId,configRevision:request.configRevision,requestId:v.identity.requestId});
 return {protocolVersion:PROTOCOL_VERSION,identity:v.identity as unknown as InspectionResponse['identity'],core,...(v.pathChecks===undefined?{}:{pathChecks:validatePathChecks(v.pathChecks)})};
}

function validateCoordinates(v:unknown){
 if(!record(v)||!strings(v,['geometry','unitSystem'])||![1,2,3].includes(Number(v.dimension))||!Array.isArray(v.axes)||v.axes.length!==3)throw new Error('Invalid Core coordinates.');
 for(const [i,a] of v.axes.entries())if(!record(a)||!strings(a,['key','displayName','nativeName','kind','blocksKey','minKey','maxKey','lowerBoundaryKey','upperBoundaryKey'])||a.key!==`x${i+1}`||typeof a.active!=='boolean'||typeof a.blocks!=='number'||(a.unit!==null&&typeof a.unit!=='string'))throw new Error('Invalid Core axis.');
}
function validatePathChecks(v:unknown):NonNullable<InspectionResponse['pathChecks']>{
 if(!Array.isArray(v)||v.length>90)throw new Error('Invalid Host path checks.');
 for(const p of v)if(!record(p)||!strings(p,['key','cwd','role','status','message'])||!['input-file','output-directory'].includes(String(p.role))||!['ok','error','not-set','unable-to-check'].includes(String(p.status))||(p.resolvedPath!==null&&typeof p.resolvedPath!=='string'))throw new Error('Invalid Host path check.');
 return v as NonNullable<InspectionResponse['pathChecks']>;
}

function validateUnit(v:unknown){
 if(!record(v)||!['known','dimensionless','unknown','not-applicable','coordinate-dependent','model-dependent','mixed-state'].includes(String(v.status))||(v.unit!==null&&typeof v.unit!=='string')||(v.axis!==undefined&&!['x1','x2','x3'].includes(String(v.axis))))throw new Error('Invalid Core unit metadata.');
}
