import {PROTOCOL_VERSION} from '../host/contracts.ts';
import {MAX_PREVIEW_BYTES} from '../host/previewContracts.ts';
import type {PreviewIdentity,RealPreviewResult} from '../host/previewContracts.ts';
import {record,validatePreviewStatus,validateRealResult} from '../host/previewValidation.ts';
import type {LinePreviewData} from './LinePreviewData.ts';
export interface WorkingCopy {text:string;filename:string;valid:boolean;dirty:boolean;hostPath?:string}
export async function configRevision(text:string){const bytes=new TextEncoder().encode(text);return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),b=>b.toString(16).padStart(2,'0')).join('');}
export async function previewRequest(route:string,body?:unknown,post=false):Promise<unknown>{
 const response=await fetch('http://127.0.0.1:4180'+route,{method:post||body?'POST':'GET',headers:{'X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION,...(body?{'Content-Type':'application/json'}:{})},body:body?JSON.stringify(body):undefined,credentials:'omit',redirect:'error',signal:AbortSignal.timeout(15000)});
 const reader=response.body?.getReader();if(!reader)throw new Error('Empty Preview response');const chunks:Uint8Array[]=[];let size=0;
 try{while(true){const {value,done}=await reader.read();if(done)break;size+=value.length;if(size>MAX_PREVIEW_BYTES+65536){await reader.cancel();throw new Error('Preview response exceeds limit');}chunks.push(value);}}finally{reader.releaseLock();}
 const bytes=new Uint8Array(size);let offset=0;for(const c of chunks){bytes.set(c,offset);offset+=c.length;}const data=JSON.parse(new TextDecoder('utf8',{fatal:true}).decode(bytes));
 if(!response.ok)throw new Error(data?.error?.message??data?.error??`Preview request failed (${response.status})`);return data;
}
export class RealInitPreviewProvider {
 async status(projectId:string){return validatePreviewStatus(await previewRequest('/api/preview/status'),projectId);}
 async start(projectId:string,text:string,profileId='sod-initial-cpu',requestedShape?:number[]){
  const revision=await configRevision(text);
  const v=await previewRequest('/api/preview',{projectId,profileId,configText:text,configRevision:revision,...(requestedShape?{requestedShape}:{})});
  if(!record(v)||v.protocolVersion!==PROTOCOL_VERSION||v.projectId!==projectId||!record(v.identity)||typeof v.requestId!=='string'||!/^[a-f0-9-]{36}$/.test(v.requestId)||v.identity.requestId!==v.requestId||v.identity.projectId!==projectId||v.identity.profileId!==profileId||v.identity.caseId!==(profileId==='cellular-initial-cpu'?'CellularDet':'Sod')||v.identity.configRevision!==revision||typeof v.identity.buildId!=='string'||typeof v.identity.binarySha256!=='string'||! /^[a-f0-9]{64}$/.test(v.identity.binarySha256))throw new Error('Invalid Preview acceptance identity');
  return v.identity as unknown as PreviewIdentity;
 }
 cancel(id:string){return previewRequest('/api/preview/'+id+'/cancel',undefined,true);}
 accept(value:unknown,identity:PreviewIdentity){return validateRealResult(value,identity);}
}
export function realInitLine(result:RealPreviewResult,key:string):LinePreviewData{
 const d=result.core.data;const f=d?.fields.find(f=>f.key===key);if(!d||d.dimension!==1||!f)throw new Error('Authoritative field unavailable');
 return {kind:'line',field:f.displayName||f.key,x:Float64Array.from(d.axes[0].values),values:Float64Array.from(f.values),min:f.min,max:f.max};
}
export function canAcceptRevision(start:{text:string;projectId:string},current:{text:string;projectId:string;valid:boolean}){return current.valid&&start.text===current.text&&start.projectId===current.projectId;}

export interface RealGrid {width:number;height:number;x:Float64Array;y:Float64Array;values:Float64Array;field:string;unit:string|null;min:number;max:number}
export function realInitGrid(result:RealPreviewResult,key:string):RealGrid{
 const d=result.core.data,f=d?.fields.find(f=>f.key===key);if(!d||d.dimension!==2||!f)throw new Error('Authoritative 2D field unavailable');
 return {width:d.sampling.shape[1],height:d.sampling.shape[0],x:Float64Array.from(d.axes[0].values),y:Float64Array.from(d.axes[1].values),values:Float64Array.from(f.values),field:f.displayName,unit:f.unit,min:f.min,max:f.max};
}
export function gridPoint(data:RealGrid,x:number,y:number){
 const xe=sampleEdges(data.x),ye=sampleEdges(data.y);
 if(!Number.isFinite(x)||!Number.isFinite(y)||x<xe[0]||x>xe[xe.length-1]||y<ye[0]||y>ye[ye.length-1])return null;
 const nearest=(a:Float64Array,v:number)=>{let index=0;for(let i=1;i<a.length;i++)if(Math.abs(a[i]-v)<Math.abs(a[index]-v))index=i;return index;};
 const i=nearest(data.x,x),j=nearest(data.y,y);return {i,j,index:j*data.width+i,x:data.x[i],y:data.y[j]};
}

// Core supplies uniform bin centers; HeatmapVis expects N+1 pixel edges.
export function sampleEdges(centers:Float64Array){const step=centers[1]-centers[0];return Float64Array.from({length:centers.length+1},(_,i)=>centers[0]+(i-.5)*step);}
