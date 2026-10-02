import {PROTOCOL_VERSION} from './contracts.ts';
export interface ConfigureStatus {
 protocolVersion:string;projectId:string;active:boolean;available?:false;profileId?:string;operationId?:string;
 latest?:{id:string;profileId:string;state:'succeeded'|'failed'|'cancelled';exitCode:number|null;error?:string};
}
function record(v:unknown):v is Record<string,unknown>{return !!v&&typeof v==='object'&&!Array.isArray(v);}
const uuid=(v:unknown)=>typeof v==='string'&&/^[a-f0-9-]{36}$/.test(v);
export function validateConfigureStatus(value:unknown,projectId:string):ConfigureStatus{
 if(!record(value)||value.protocolVersion!==PROTOCOL_VERSION||value.projectId!==projectId||typeof value.active!=='boolean')
  throw new Error('Incompatible or stale Configure response.');
 if(value.available===false){
  if(value.active||value.profileId!==undefined||value.operationId!==undefined||value.latest!==undefined)throw new Error('Unavailable Configure has operation state.');
 }else{
  if(typeof value.profileId!=='string'||!value.profileId)throw new Error('Missing Host Configure profile.');
  if(value.operationId!==undefined&&!uuid(value.operationId))throw new Error('Invalid Configure operation ID.');
  if(value.active&&!uuid(value.operationId))throw new Error('Active Configure has no operation ID.');
  if(value.latest!==undefined){
   const r=value.latest;
   if(!record(r)||!uuid(r.id)||r.profileId!==value.profileId||!['succeeded','failed','cancelled'].includes(String(r.state))||
      (r.exitCode!==null&&!Number.isInteger(r.exitCode))||(r.error!==undefined&&typeof r.error!=='string')||
      (!value.active&&r.id!==value.operationId)||(r.state==='succeeded'&&r.exitCode!==0))
    throw new Error('Malformed Configure result.');
  }
 }
 return value as unknown as ConfigureStatus;
}
