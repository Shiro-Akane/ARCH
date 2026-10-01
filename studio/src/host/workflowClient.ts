import {PROTOCOL_VERSION} from './contracts.ts';
import {record} from './previewValidation.ts';
import {sameBuildScope} from './configurationContracts.ts';
import type {ConfigurationBuildScope,ConfigurationIdentity} from './configurationContracts.ts';
import {validateRegistry,validateWorkflowCore} from './workflowValidation.ts';
import type {DiscoveryResponse,WorkflowOperation,WorkflowResult,WorkflowStatus} from './workflowContracts.ts';
export function validateDiscovery(value:unknown,scope:ConfigurationBuildScope):DiscoveryResponse{
 if(!record(value)||value.protocolVersion!==PROTOCOL_VERSION||!sameBuildScope(value as unknown as ConfigurationBuildScope,scope)||!Array.isArray(value.fieldModels))throw new Error('Discovery build identity mismatch.');
 validateRegistry({schemaVersion:'1.0',version:'1',kind:'registered-cases',status:'ok',setup:'not_executed',cuda:'not_initialized',cases:value.cases});
 for(const m of value.fieldModels)if(!record(m)||typeof m.caseId!=='string'||!Array.isArray(m.dimensions)||m.dimensions.some(d=>![1,2,3].includes(Number(d)))||!Array.isArray(m.geometries)||!m.geometries.every(g=>typeof g==='string'))throw new Error('Invalid field discovery.');
 if(value.amr!==null){const a=value.amr;if(!record(a)||!Array.isArray(a.cases)||!a.cases.every(c=>typeof c==='string')||!Array.isArray(a.geometries)||!a.geometries.every(g=>typeof g==='string')||!Number.isInteger(a.defaultMaxBlocks)||Number(a.defaultMaxBlocks)<1||Number(a.defaultMaxBlocks)>1024||!Number.isInteger(a.defaultMemoryMiB)||Number(a.defaultMemoryMiB)<16||Number(a.defaultMemoryMiB)>256)throw new Error('Invalid AMR discovery.');}
 return value as unknown as DiscoveryResponse;
}
export function validateWorkflowAck(value:unknown,scope:ConfigurationBuildScope,caseId:string,revision:string):ConfigurationIdentity{
 if(!record(value)||value.protocolVersion!==PROTOCOL_VERSION||!record(value.identity)||!sameBuildScope(value.identity as unknown as ConfigurationBuildScope,scope)||value.identity.caseId!==caseId||value.identity.configRevision!==revision||typeof value.identity.requestId!=='string'||!/^[a-f0-9-]{36}$/.test(value.identity.requestId))throw new Error('Workflow acknowledgement identity mismatch.');
 return value.identity as unknown as ConfigurationIdentity;
}
export function validateWorkflowStatus(value:unknown,projectId:string):WorkflowStatus{
 if(!record(value)||value.protocolVersion!==PROTOCOL_VERSION||value.projectId!==projectId||!['none','running','succeeded','failed','cancelled'].includes(String(value.state))||(value.stage!==undefined&&typeof value.stage!=='string')||(value.error!==undefined&&typeof value.error!=='string'))throw new Error('Invalid workflow status.');
 return value as unknown as WorkflowStatus;
}
export function acceptWorkflowResult(value:unknown,identity:ConfigurationIdentity,operation:WorkflowOperation):WorkflowResult{
 if(!record(value)||value.operation!==operation||!record(value.identity)||!Object.entries(identity).every(([k,v])=>record(value.identity)&&value.identity[k]===v))throw new Error('Workflow result identity mismatch.');
 return {identity,operation,core:validateWorkflowCore(value.core,operation,{requestId:identity.requestId,caseId:identity.caseId,configRevision:identity.configRevision})};
}
