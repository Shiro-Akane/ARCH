import {copyOverviewRequest,validOverview} from './plotfileOverview.ts';
import type {PlotfileOverview,PlotfileOverviewRequest} from './plotfileOverview.ts';
import {sourceEvidenceValid} from './plotfileSourceIdentity.ts';
import type {PlotfileSourceEvidence} from './plotfileSourceIdentity.ts';
import {hostEndpoint} from './desktop.ts';
import {PROTOCOL_VERSION} from './contracts.ts';
import {record} from './previewValidation.ts';
export interface SliceSelection {field:string;block:number;start:number[];count:number[]}
export type RawPlotNumber=number|'NaN'|'Infinity'|'-Infinity';
export interface CandidateNativeGrid {
 version:'candidate-cartesian-1';measureSource:'GridMetrics::CellVolume';
 measureConvention:'active-coordinate-product; inactive-measures-omitted';measureUnit:null;
}
export interface NativePlotCells extends CandidateNativeGrid {
 identityScope:'file-local';logicalKey:string;level:number;logicalCoordinates:number[];
 lower:Record<'x1'|'x2'|'x3',number[]>;upper:Record<'x1'|'x2'|'x3',number[]>;cellMeasure:number[];
}
export interface PlotfileAudit {
 overview?:PlotfileOverview;
 candidateSourceIdentity?:PlotfileSourceEvidence|null;
 schemaVersion:string;file:{bytes:number;sha256:string};time:number;dimension:number;geometry:string;
 blocks:number;cellShape:number[];cells:number;fields:{name:string;shape:number[];unit:null}[];
 completion:{state:'unknown';reason:string};renderEligible:false;candidateNativeGrid?:CandidateNativeGrid|null;
 scientificIdentity:Record<string,null>;coordinates:{storedBasis:'cartesian';centering:'cell-center';units:null};
 payload?:{nativeCells?:NativePlotCells|null;field:string;block:number;start:number[];shape:number[];linearIndices:number[];values:RawPlotNumber[];coordinates:Record<'x'|'y'|'z',RawPlotNumber[]>;unit:null;diagnostics:string[]};
}
export interface AuditResponse {projectId:string;relativePath:string;audit:PlotfileAudit}
const raw=(v:unknown):v is RawPlotNumber=>typeof v==='number'&&Number.isFinite(v)||v==='NaN'||v==='Infinity'||v==='-Infinity';
function unknownScience(v:unknown){return record(v)&&['case','config','build','binary','eos'].every(k=>v[k]===null);}
function rawCoordinates(v:unknown,n:number){if(!record(v))return false;return ['x','y','z'].every(k=>{const a=v[k];return Array.isArray(a)&&a.length===n&&a.every(raw);});}
function nativeHeaderValid(v:unknown):v is CandidateNativeGrid {
 return record(v)&&v.version==='candidate-cartesian-1'&&v.measureSource==='GridMetrics::CellVolume'&&
  v.measureConvention==='active-coordinate-product; inactive-measures-omitted'&&v.measureUnit===null;
}
function nativeCellsValid(v:unknown,header:unknown,n:number,dimension:number):v is NativePlotCells {
 if(!nativeHeaderValid(v)||!nativeHeaderValid(header)||!record(v)||v.identityScope!=='file-local'||
    !Number.isSafeInteger(v.level)||Number(v.level)<0||!Array.isArray(v.logicalCoordinates)||
    v.logicalCoordinates.length!==3||v.logicalCoordinates.some(x=>!Number.isSafeInteger(x)||x<0||x>0xffffffff)||
    v.logicalKey!==[v.level,...v.logicalCoordinates].join('/')||
    !record(v.lower)||!record(v.upper)||!Array.isArray(v.cellMeasure)||v.cellMeasure.length!==n||
    v.cellMeasure.some(x=>typeof x!=='number'||!Number.isFinite(x)||x<=0))return false;
 const lower=v.lower,upper=v.upper;
 return ['x1','x2','x3'].every((key,axis)=>{
  const lo=lower[key],hi=upper[key];
  return Array.isArray(lo)&&Array.isArray(hi)&&lo.length===n&&hi.length===n&&
   lo.every((x,i)=>typeof x==='number'&&Number.isFinite(x)&&typeof hi[i]==='number'&&Number.isFinite(hi[i])&&
    (axis<dimension?hi[i]>x:x===0&&hi[i]===0));
 });
}
/** Reverse the stored x1-fastest index into no-ghost block-local i/j/k. */
export function storedCellIndices(a:Pick<PlotfileAudit,'cellShape'>,block:number,index:number):number[] {
 const blockCells=a.cellShape.reduce((x,y)=>x*y,1);let local=index-block*blockCells;
 if(!Number.isSafeInteger(local)||local<0||local>=blockCells)throw Error('Stored sample outside block.');
 const ijk=[0,0,0];
 for(let axis=a.cellShape.length-1;axis>=0;axis--){
  ijk[a.cellShape.length-1-axis]=local%a.cellShape[axis];local=Math.floor(local/a.cellShape[axis]);
 }
 return ijk;
}
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
 if(a.candidateSourceIdentity!==undefined&&a.candidateSourceIdentity!==null&&!sourceEvidenceValid(a.candidateSourceIdentity))
  throw Error('Invalid candidate source evidence.');
 const hasNative=a.candidateNativeGrid!==undefined&&a.candidateNativeGrid!==null;
 if(hasNative&&(!nativeHeaderValid(a.candidateNativeGrid)||a.geometry!=='cartesian'||![1,2].includes(Number(a.dimension))))
  throw Error('Invalid candidate native header.');
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
  if(hasNative?!nativeCellsValid(p.nativeCells,a.candidateNativeGrid,n,Number(a.dimension)):
     p.nativeCells!==undefined&&p.nativeCells!==null)
   throw Error('Plotfile native cell payload/header mismatch.');
 }else if(a.payload!==undefined)throw Error('Metadata response unexpectedly contains raw samples.');
 return {projectId,relativePath,audit:a as unknown as PlotfileAudit};
}
async function fetchPlotfileAudit(projectId:string,relativePath:string,signal:AbortSignal,selection?:SliceSelection,expectedSha?:string,overview?:PlotfileOverviewRequest):Promise<unknown>{
 const response=await fetch(hostEndpoint+'/api/plotfile/audit-'+(overview?'overview':selection?'slice':'metadata'),{
  method:'POST',headers:{'X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION,'Content-Type':'application/json'},
  credentials:'omit',redirect:'error',signal:AbortSignal.any([signal,AbortSignal.timeout(16000)]),
  body:JSON.stringify({projectId,relativePath,...(overview?{overview:copyOverviewRequest(overview),expectedFileSha256:expectedSha}:selection?{slice:selection,expectedFileSha256:expectedSha}:{})}),
 });
 if(!response.headers.get('content-type')?.includes('application/json'))throw Error('Invalid Plotfile response type.');
 const reader=response.body?.getReader();if(!reader)throw Error('Empty Plotfile response.');
 const chunks:Uint8Array[]=[];let bytes=0;
 try{while(true){const {value,done}=await reader.read();if(done)break;bytes+=value.length;if(bytes>128*1024){await reader.cancel();throw Error('Plotfile response exceeds budget.');}chunks.push(value);}}finally{reader.releaseLock();}
 const data=new Uint8Array(bytes);let offset=0;for(const c of chunks){data.set(c,offset);offset+=c.length;}
 const value:unknown=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(data));
 if(!response.ok)throw Error(record(value)&&record(value.error)&&typeof value.error.message==='string'?value.error.message:'Plotfile read failed ('+response.status+').');
 return value;
}

export async function requestPlotfileAudit(projectId:string,relativePath:string,signal:AbortSignal,selection?:SliceSelection,expectedSha?:string):Promise<AuditResponse>{
 return validatePlotfileAudit(await fetchPlotfileAudit(projectId,relativePath,signal,selection,expectedSha),projectId,relativePath,selection,expectedSha);
}
export function validatePlotfileOverview(value:unknown,projectId:string,relativePath:string,request:PlotfileOverviewRequest,sha:string):AuditResponse{
 if(!record(value)||!record(value.result)||value.result.schemaVersion!=='audit-overview-1'||
  value.result.payload!==undefined||!record(value.result.file)||value.result.file.sha256!==sha)
  throw Error('Plotfile overview identity mismatch.');
 const result=value.result,{overview,...metadata}=result;
 const response=validatePlotfileAudit({...value,metadata:{...metadata,schemaVersion:'audit-1'}},projectId,relativePath);
 if(!response.audit.candidateNativeGrid||!response.audit.fields.some(f=>f.name===request.field)||
  !validOverview(overview,copyOverviewRequest(request),response.audit.cells,response.audit.dimension,response.audit.cellShape,response.audit.blocks)||
  overview.nativeBlocks!==undefined&&overview.nativeBlocks.totalBlocks!==response.audit.blocks)
  throw Error('Invalid candidate Plotfile overview.');
 return {...response,audit:{...response.audit,schemaVersion:'audit-overview-1',overview}};
}
export async function requestPlotfileOverview(projectId:string,relativePath:string,signal:AbortSignal,request:PlotfileOverviewRequest,sha:string){
 return validatePlotfileOverview(await fetchPlotfileAudit(projectId,relativePath,signal,undefined,sha,request),projectId,relativePath,request,sha);
}
