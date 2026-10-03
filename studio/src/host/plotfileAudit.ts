import {hostEndpoint} from './desktop.ts';
import {PROTOCOL_VERSION} from './contracts.ts';
import {record} from './previewValidation.ts';
export interface SliceSelection {field:string;block:number;start:number[];count:number[]}
export type RawPlotNumber=number|'NaN'|'Infinity'|'-Infinity';
export interface PlotfileAudit {
 schemaVersion:string;file:{bytes:number;sha256:string};time:number;dimension:number;geometry:string;
 blocks:number;cellShape:number[];cells:number;fields:{name:string;shape:number[];unit:null}[];
 completion:{state:'unknown';reason:string};renderEligible:false;
 scientificIdentity:Record<string,null>;coordinates:{storedBasis:'cartesian';centering:'cell-center';units:null};
 payload?:{field:string;block:number;start:number[];shape:number[];linearIndices:number[];values:RawPlotNumber[];coordinates:Record<'x'|'y'|'z',RawPlotNumber[]>;unit:null;diagnostics:string[]};
}
export interface AuditResponse {projectId:string;relativePath:string;audit:PlotfileAudit}
const raw=(v:unknown):v is RawPlotNumber=>typeof v==='number'&&Number.isFinite(v)||v==='NaN'||v==='Infinity'||v==='-Infinity';
function unknownScience(v:unknown){return record(v)&&['case','config','build','binary','eos'].every(k=>v[k]===null);}
function rawCoordinates(v:unknown,n:number){if(!record(v))return false;return ['x','y','z'].every(k=>{const a=v[k];return Array.isArray(a)&&a.length===n&&a.every(raw);});}
export function validatePlotfileAudit(value:unknown,projectId:string,relativePath:string,selection?:SliceSelection,expectedSha?:string):AuditResponse{
 if(!record(value)||value.protocolVersion!==PROTOCOL_VERSION||value.projectId!==projectId||value.relativePath!==relativePath)
  throw Error('Plotfile response project/file identity mismatch.');
 const a=selection?value.result:value.metadata;
 if(!record(a)||a.schemaVersion!==(selection?'audit-slice-1':'audit-1')||!record(a.file)||
    typeof a.file.sha256!=='string'||!/^[a-f0-9]{64}$/.test(a.file.sha256)||!Number.isSafeInteger(a.file.bytes)||Number(a.file.bytes)<1||Number(a.file.bytes)>64*1024*1024||
    typeof a.time!=='number'||!Number.isFinite(a.time)||a.time<0||![1,2,3].includes(Number(a.dimension))||
    !['cartesian','cylindrical','spherical'].includes(String(a.geometry))||!Number.isSafeInteger(a.blocks)||Number(a.blocks)<1||
    !Array.isArray(a.cellShape)||a.cellShape.length!==a.dimension||a.cellShape.some(n=>!Number.isSafeInteger(n)||n<1)||
    !Number.isSafeInteger(a.cells)||Number(a.cells)<1||Number(a.cells)>10_000_000||
    a.cells!==Number(a.blocks)*a.cellShape.reduce((x:number,y:number)=>x*y,1)||
    !Array.isArray(a.fields)||a.fields.length<1||a.fields.length>128||
    a.fields.some(f=>!record(f)||typeof f.name!=='string'||!f.name.length||f.unit!==null||JSON.stringify(f.shape)!==JSON.stringify([a.blocks,...a.cellShape as number[]]))||
    !record(a.completion)||a.completion.state!=='unknown'||typeof a.completion.reason!=='string'||a.renderEligible!==false||
    !unknownScience(a.scientificIdentity)||
    !record(a.coordinates)||a.coordinates.storedBasis!=='cartesian'||a.coordinates.centering!=='cell-center'||a.coordinates.units!==null)
  throw Error('Unsupported or malformed Plotfile audit response.');
 if(selection){
  const cellShape=a.cellShape as number[];
  if(selection.start.length!==cellShape.length||selection.count.length!==cellShape.length||
     !Number.isSafeInteger(selection.block)||selection.block<0||selection.block>=Number(a.blocks)||
     selection.start.some((n,i)=>!Number.isSafeInteger(n)||n<0||!Number.isSafeInteger(selection.count[i])||selection.count[i]<1||n+selection.count[i]>cellShape[i])||
     !(a.fields as {name:string}[]).some(f=>f.name===selection.field))throw Error('Invalid slice selection for stored file shape.');
  const p=a.payload,n=selection.count.reduce((x,y)=>x*y,1);
  const expectedIndices=Array.from({length:Math.min(n,512)},(_,i)=>{
   let local=i,index=selection.block*cellShape.reduce((x,y)=>x*y,1),stride=1;
   for(let axis=cellShape.length-1;axis>=0;axis--){index+=(selection.start[axis]+local%selection.count[axis])*stride;local=Math.floor(local/selection.count[axis]);stride*=cellShape[axis];}
   return index;
  });
  if(a.file.sha256!==expectedSha||!record(p)||p.field!==selection.field||p.block!==selection.block||p.order!=='x1-fastest'||p.unit!==null||
     JSON.stringify(p.start)!==JSON.stringify(selection.start)||JSON.stringify(p.shape)!==JSON.stringify(selection.count)||
     n<1||n>512||!Array.isArray(p.values)||p.values.length!==n||!p.values.every(raw)||
     !Array.isArray(p.linearIndices)||p.linearIndices.length!==n||p.linearIndices.some((v,i)=>v!==expectedIndices[i])||
     !rawCoordinates(p.coordinates,n)||
     !Array.isArray(p.diagnostics)||!p.diagnostics.every(d=>typeof d==='string'))
   throw Error('Plotfile slice identity or raw payload mismatch.');
 }else if(a.payload!==undefined)throw Error('Metadata response unexpectedly contains raw samples.');
 return {projectId,relativePath,audit:a as unknown as PlotfileAudit};
}
export async function requestPlotfileAudit(projectId:string,relativePath:string,signal:AbortSignal,selection?:SliceSelection,expectedSha?:string):Promise<AuditResponse>{
 const response=await fetch(hostEndpoint+'/api/plotfile/audit-'+(selection?'slice':'metadata'),{
  method:'POST',headers:{'X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION,'Content-Type':'application/json'},
  credentials:'omit',redirect:'error',signal:AbortSignal.any([signal,AbortSignal.timeout(16000)]),
  body:JSON.stringify({projectId,relativePath,...(selection?{slice:selection,expectedFileSha256:expectedSha}:{})}),
 });
 if(!response.headers.get('content-type')?.includes('application/json'))throw Error('Invalid Plotfile response type.');
 const reader=response.body?.getReader();if(!reader)throw Error('Empty Plotfile response.');
 const chunks:Uint8Array[]=[];let bytes=0;
 try{while(true){const {value,done}=await reader.read();if(done)break;bytes+=value.length;if(bytes>128*1024){await reader.cancel();throw Error('Plotfile response exceeds budget.');}chunks.push(value);}}finally{reader.releaseLock();}
 const data=new Uint8Array(bytes);let offset=0;for(const c of chunks){data.set(c,offset);offset+=c.length;}
 const value:unknown=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(data));
 if(!response.ok)throw Error(record(value)&&record(value.error)&&typeof value.error.message==='string'?value.error.message:'Plotfile read failed ('+response.status+').');
 return validatePlotfileAudit(value,projectId,relativePath,selection,expectedSha);
}
