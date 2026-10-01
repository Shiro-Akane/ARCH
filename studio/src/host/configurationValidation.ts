import {PROTOCOL_VERSION} from './contracts.ts';
import {record} from './previewValidation.ts';
import type {ConfigurationBuildScope,ConfigurationInspection,ConfigurationSchema,ConfigurationRequest,InspectionResponse,SchemaResponse} from './configurationContracts.ts';
import {sameBuildScope} from './configurationContracts.ts';
const scalar=(v:unknown)=>typeof v==='string'||typeof v==='boolean'||typeof v==='number'&&Number.isFinite(v);
const strings=(v:Record<string,unknown>,keys:string[])=>keys.every(k=>typeof v[k]==='string');
const nullableText=(v:unknown)=>v===null||typeof v==='string';
const stringArray=(v:unknown):v is string[]=>Array.isArray(v)&&v.length<=65536&&v.every(x=>typeof x==='string');
function typedValue(value:unknown,type:unknown){
 return value===null||type==='string'&&typeof value==='string'||type==='bool'&&typeof value==='boolean'
  ||(type==='float'||type==='expression')&&typeof value==='number'&&Number.isFinite(value)
  ||type==='int'&&typeof value==='number'&&Number.isInteger(value)&&value>=-2147483648&&value<=2147483647;
}
function fail(message:string):never {throw new Error(message);}
function version(v:unknown){
 if(!record(v)||v.schemaVersion!=='1.0'||v.version!=='3')
  fail('Incompatible Core configuration version. Update Core and Studio together; Working Copy is retained.');
}
function condition(v:unknown){
 if(!record(v)||!strings(v,['id','description'])||!stringArray(v.dependencies))fail('Malformed Core condition.');
}
function state(v:unknown,requirement=false){
 if(!record(v)||typeof v.conditionId!=='string'||!['satisfied','not-applicable','unknown-dependency'].includes(String(v.state))||!stringArray(v.missingDependencies))fail('Malformed condition state.');
 if(v.state==='unknown-dependency'?v.missingDependencies.length===0:v.missingDependencies.length!==0)fail('Inconsistent condition dependencies.');
 if(requirement&&(v.required!==(v.state==='unknown-dependency'?null:v.state==='satisfied')))fail('Inconsistent required state.');
}
function locations(v:unknown){
 if(!Array.isArray(v)||v.length>65536)fail('Malformed source locations.');
 for(const p of v)if(!record(p)||typeof p.source!=='string'||!['line','column','endColumn'].every(k=>typeof p[k]==='number'&&Number.isSafeInteger(p[k])&&(p[k] as number)>0)||Number(p.endColumn)<Number(p.column)||!nullableText(p.rawValue))fail('Malformed source location.');
}
function path(v:unknown){
 if(v!==null&&(!record(v)||v.checkOwner!=='local-host'||v.relativeTo!=='process-working-directory'||v.existenceChecked!==false||typeof v.targetMayBeNew!=='boolean'||!['input-file','output-directory'].includes(String(v.role))))fail('Invalid Core path authority.');
}
function parameterList(v:unknown,caseId:string|null){
 if(!Array.isArray(v)||v.length>4096)fail('Malformed parameter catalog.');
 const keys=new Set<string>();
 for(const p of v){
  if(!record(p)||!strings(p,['key','type','group','usage'])||!/^[A-Za-z_][A-Za-z0-9_]*$/.test(String(p.key))||keys.has(String(p.key))||p.caseId!==caseId||!['simulation','verification'].includes(String(p.usage))||!['int','float','bool','string','expression'].includes(String(p.type))||!record(p.constraints)||!record(p.units)||(p.options!==null&&!record(p.options))||!record(p.requirement)||!['required','conditional','optional'].includes(String(p.requirement.kind))||'defaultValue' in p||'defaultSource' in p||'aliasOf' in p)fail('Malformed standard parameter schema.');
  condition(p.requirement.condition);condition(p.applicability);
  if(p.allowedDefault!==null&&(!record(p.allowedDefault)||!scalar(p.allowedDefault.value)||p.allowedDefault.source!=='documented-default'||typeof p.allowedDefault.evidence!=='string'||p.requirement.kind!=='optional'))fail('Invalid allowed default.');
  if(!Array.isArray(p.templateRecommendations)||p.templateRecommendations.some(x=>!record(x)||!scalar(x.value)||typeof x.evidence!=='string'))fail('Invalid template recommendation.');
  for(const key of ['min','max','storageMin','storageMax'])if(p.constraints[key]!==undefined&&(typeof p.constraints[key]!=='number'||!Number.isFinite(p.constraints[key])))fail('Malformed parameter constraint.');
  if(p.presentation!==undefined&&(!record(p.presentation)||!strings(p.presentation,['displayName','description','subgroup'])))fail('Invalid parameter presentation.');
  if(record(p.presentation)){
   if(p.presentation.enabledBy!==undefined&&typeof p.presentation.enabledBy!=='string')fail('Invalid parameter enablement.');
   const t=p.presentation.toggle;
   if(t!==undefined&&(!record(t)||t.enabledWhen!=='value > 0'||typeof t.offValue!=='number'||!Number.isFinite(t.offValue)||t.enabledValueRequired!==true||t.preserveUneditedInput!==true))fail('Unsupported parameter toggle.');
  }
  validateUnit(p.units);path(p.path);keys.add(String(p.key));
 }
}
export function validateConfigurationSchema(v:unknown):ConfigurationSchema {
 version(v);
 if(!record(v)||v.kind!=='configuration-schema'||v.status!=='ok'||v.standardParametersComplete!==true||v.constraintsComplete!==false||!Array.isArray(v.parameters)||v.parameters.length<1||typeof v.caseDeclarationsComplete!=='boolean'||!stringArray(v.retiredKeys)||!Array.isArray(v.caseDeclarations)||v.caseDeclarations.length>4096)fail('Unsupported Core configuration schema.');
 parameterList(v.parameters,null);parameterList(v.auxiliaryParameters,null);
 const ids=new Set<string>();
 for(const c of v.caseDeclarations){
  if(!record(c)||!strings(c,['caseId','source','sourceSha256'])||ids.has(String(c.caseId))||!/^[0-9a-f]{64}$/.test(String(c.sourceSha256))||typeof c.declarationsComplete!=='boolean'||!record(c.composition)||typeof c.composition.status!=='string'||typeof c.composition.inspectionRequired!=='boolean')fail('Malformed case declaration.');
  parameterList(c.parameters,String(c.caseId));ids.add(String(c.caseId));
 }
 if(v.coordinateSystems!==undefined){if(!Array.isArray(v.coordinateSystems)||v.coordinateSystems.length>64)fail('Invalid coordinate catalog.');v.coordinateSystems.forEach(validateCoordinates);}
 return v as unknown as ConfigurationSchema;
}
export function validateConfigurationInspection(v:unknown,expected:{caseId:string;configRevision:string;requestId:string}):ConfigurationInspection {
 version(v);
 if(!record(v)||v.kind!=='configuration-inspection'||!['ok','error'].includes(String(v.status))||!record(v.identity)||!Object.entries(expected).every(([k,x])=>record(v.identity)&&v.identity[k]===x)||!record(v.execution)||v.execution.setup!=='not_executed'||v.execution.simulationReadiness!=='not_checked'||v.execution.eos!=='not_loaded'||v.execution.filesystem!=='not_accessed'||v.execution.cuda!=='not_initialized'||!record(v.coverage)||!['standardParametersComplete','auxiliaryParametersComplete','caseParametersComplete','conditionsComplete','diagnosticsComplete'].every(k=>typeof (v.coverage as Record<string,unknown>)[k]==='boolean')||!record(v.completeness)||v.completeness.scope!=='declared-configuration-before-setup'||!['complete','incomplete','invalid','undetermined'].includes(String(v.completeness.state))||!Array.isArray(v.diagnostics)||v.diagnostics.length>65536)fail('Invalid Core inspection contract or identity.');
 if(v.diffusion!==undefined||v.amrIndicators!==undefined)fail('Unsupported configuration derived metadata; update the matching Core/Studio contract.');
 if(!['syntax','typed-input','conditional-resolution'].includes(String(v.execution.validationStage))||!['checked','not_checked'].includes(String(v.execution.caseRegistration))||!['checked','not_checked'].includes(String(v.execution.caseDeclarations)))fail('Invalid configuration execution coverage.');
 const omitted=v.parameters===undefined&&v.status==='error'&&Object.values(v.coverage).every(x=>x===false)&&v.diagnostics.some(d=>record(d)&&d.code==='RESPONSE_TOO_LARGE');
 const parameters=omitted?[]:v.parameters;
 if(!Array.isArray(parameters)||parameters.length>4096)fail('Malformed inspection parameters.');
 const keys=new Set<string>();
 for(const p of parameters){
  if(!record(p)||!strings(p,['key','type','group','usage'])||(p.caseId!==null&&p.caseId!==expected.caseId)||!['simulation','verification'].includes(String(p.usage))||!['int','float','bool','string','expression'].includes(String(p.type))||keys.has(String(p.key))||!['missing','present','invalid','duplicate'].includes(String(p.inputState))||!nullableText(p.rawValue)||(p.parsedValue!==null&&!scalar(p.parsedValue))||(p.resolvedValue!==null&&!scalar(p.resolvedValue))||(p.valueSource!==null&&!['input','case-defined','derived','documented-default'].includes(String(p.valueSource)))||p.valueStage!=='configuration-resolution-before-setup'||'defaultValue'in p||'applicable'in p)fail('Malformed parsed parameter.');
  if(!typedValue(p.parsedValue,p.type)||!typedValue(p.resolvedValue,p.type))fail('Value disagrees with Core parameter type.');
  locations(p.locations);state(p.requirement,true);state(p.applicability);validateUnit(p.units);path(p.path);
  if(p.sourceEvidence!==null&&(!record(p.sourceEvidence)||typeof p.sourceEvidence.owner!=='string'||!stringArray(p.sourceEvidence.dependencies)))fail('Malformed value source evidence.');
  const loc=p.locations as unknown[];
  if((p.resolvedValue===null)!==(p.valueSource===null))fail('Unresolved value has a false source.');
  if(p.valueSource===null&&p.sourceEvidence!==null)fail('Unresolved value has source evidence.');
  if(['case-defined','derived','documented-default'].includes(String(p.valueSource))&&p.sourceEvidence===null)fail('Missing authoritative source evidence.');
  if(p.inputState==='missing'&&(p.rawValue!==null||p.parsedValue!==null||loc.length!==0||p.valueSource==='input'))fail('Missing input was filled as parsed data.');
  if(p.inputState==='duplicate'&&(p.rawValue!==null||p.parsedValue!==null||p.resolvedValue!==null||loc.length<2))fail('Duplicate input selected an occurrence.');
  if(p.inputState==='invalid'&&p.resolvedValue!==null)fail('Invalid input has an effective value.');
  if(p.inputState==='present'&&(typeof p.rawValue!=='string'||p.parsedValue===null||loc.length!==1||p.valueSource!=='input'))fail('Malformed explicit input.');
  keys.add(String(p.key));
 }
 for(const d of v.diagnostics){
  if(!record(d)||!strings(d,['severity','code','message'])||!['info','warning','error'].includes(String(d.severity))||!nullableText(d.parameterKey)||!nullableText(d.module)||!nullableText(d.conditionId)||!record(d.expected)||!nullableText(d.expected.type)||!stringArray(d.relatedKeys))fail('Malformed configuration diagnostic.');
  locations(d.locations);if(d.expected.units!==null)validateUnit(d.expected.units);
 }
 if(v.status==='ok'&&(v.completeness.state!=='complete'||!Object.values(v.coverage).every(x=>x===true)||v.diagnostics.some(d=>record(d)&&d.severity==='error')))fail('Incomplete inspection claimed success.');
 if(v.status==='error'&&v.completeness.state==='complete')fail('Error inspection claimed completeness.');
 if(v.coordinates!==undefined&&v.coordinates!==null)validateCoordinates(v.coordinates);
 return {...v,parameters} as unknown as ConfigurationInspection;
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
 if(!Array.isArray(v)||v.length>4096)throw new Error('Invalid Host path checks.');
 for(const p of v)if(!record(p)||!strings(p,['key','cwd','role','status','message'])||!['input-file','output-directory'].includes(String(p.role))||!['ok','error','not-set','unable-to-check'].includes(String(p.status))||(p.resolvedPath!==null&&typeof p.resolvedPath!=='string'))throw new Error('Invalid Host path check.');
 return v as NonNullable<InspectionResponse['pathChecks']>;
}

function validateUnit(v:unknown){
 if(!record(v)||!['known','dimensionless','unknown','not-applicable','coordinate-dependent','model-dependent','mixed-state','not-specified'].includes(String(v.status))||(v.unit!==null&&typeof v.unit!=='string')||(v.axis!==undefined&&!['x1','x2','x3'].includes(String(v.axis))))throw new Error('Invalid Core unit metadata.');
}
