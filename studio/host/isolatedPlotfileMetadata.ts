import {copyOverviewRequest,validOverview} from '../src/host/plotfileOverview.ts';
import type {PlotfileOverviewRequest} from '../src/host/plotfileOverview.ts';
/** Linux reader isolation only; no endpoint, renderer or completion claim. */
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {copyPlotfileSliceRequest} from './plotfileSliceRequest.ts';
import type {PlotfileSliceRequest} from './plotfileSliceRequest.ts';
import type {inspectPlotfileMetadata} from './plotfileMetadata.ts';

type Metadata=Awaited<ReturnType<typeof inspectPlotfileMetadata>>;
export type PlotfileReadCode='BUSY'|'CANCELLED'|'TIMEOUT'|'OUTPUT_LIMIT'|'WORKER_FAILED'|'INVALID_RESPONSE';
export class PlotfileReadError extends Error {
 readonly code:PlotfileReadCode;
 constructor(code:PlotfileReadCode,message:string){super(message);this.name='PlotfileReadError';this.code=code;}
}
let active=false;
const OUTPUT_LIMIT=64*1024;

/** Options are Host-owned. The browser must not supply execution settings. */
function readIsolated(path:string,options:{signal?:AbortSignal;timeoutMs?:number}={},slice?:PlotfileSliceRequest,overview?:PlotfileOverviewRequest):Promise<Metadata>{
 const timeoutMs=options.timeoutMs??15_000;
 if(!Number.isSafeInteger(timeoutMs)||timeoutMs<1||timeoutMs>15_000)
  return Promise.reject(new RangeError('Metadata timeout must be 1..15000 ms.'));
 if(options.signal?.aborted)return Promise.reject(new PlotfileReadError('CANCELLED','Metadata read cancelled.'));
 if(active)return Promise.reject(new PlotfileReadError('BUSY','A metadata read is already active.'));
 active=true;
 return new Promise((resolve,reject)=>{
  let failure:Error|undefined;let bytes=0;const chunks:Buffer[]=[];
  // The heap cap is not a hard RSS/WASM cap. Process termination bounds lifetime.
  const child=spawn(process.execPath,['--max-old-space-size=256',fileURLToPath(new URL('./plotfileMetadataWorker.ts',import.meta.url)),path,...(overview?[JSON.stringify({overview})]:slice?[JSON.stringify(slice)]:[])],
   {stdio:['ignore','pipe','ignore'],shell:false});
  const terminate=(error:Error)=>{failure??=error;child.kill('SIGKILL');};
  const cancel=()=>terminate(new PlotfileReadError('CANCELLED','Metadata read cancelled.'));
  const timer=setTimeout(()=>terminate(new PlotfileReadError('TIMEOUT','Metadata read exceeded its wall-clock budget.')),timeoutMs);
  options.signal?.addEventListener('abort',cancel,{once:true});
  // Covers cancellation between the initial check and listener registration.
  if(options.signal?.aborted)cancel();
  child.on('error',error=>{failure??=new PlotfileReadError('WORKER_FAILED',error.message);});
  child.stdout.on('data',(chunk:Buffer)=>{
   bytes+=chunk.length;
   if(bytes>OUTPUT_LIMIT){terminate(new PlotfileReadError('OUTPUT_LIMIT','Metadata worker response exceeded 64 KiB.'));return;}
   if(!failure)chunks.push(chunk);
  });
  child.on('close',(code,signal)=>{
   clearTimeout(timer);options.signal?.removeEventListener('abort',cancel);
   active=false;
   // Settle only after process exit, including cancellation and timeout.
   if(failure){reject(failure);return;}
   try {
    const response=JSON.parse(Buffer.concat(chunks).toString('utf8'));
    if(code!==0||signal||response.ok!==true){
     reject(new PlotfileReadError('WORKER_FAILED',typeof response.message==='string'?response.message:'Metadata worker failed.'));return;
    }
    if(response.result?.schemaVersion!==(overview?'audit-overview-1':slice?'audit-slice-1':'audit-1')||response.result?.renderEligible!==false||
       response.result?.completion?.state!=='unknown')throw Error('Invalid metadata worker response.');
    if(overview&&!validOverview(response.result.overview,overview,response.result.cells,response.result.dimension))throw Error('Invalid overview worker response.');
    if(slice){
     const p=response.result.payload,n=slice.count.reduce((a,b)=>a*b,1);
     const raw=(v:unknown)=>typeof v==='number'&&Number.isFinite(v)||v==='NaN'||v==='Infinity'||v==='-Infinity';
     if(!p||p.field!==slice.field||p.block!==slice.block||p.order!=='x1-fastest'||
        JSON.stringify(p.start)!==JSON.stringify(slice.start)||JSON.stringify(p.shape)!==JSON.stringify(slice.count)||
        !Array.isArray(p.values)||p.values.length!==n||!p.values.every(raw)||
        !Array.isArray(p.linearIndices)||p.linearIndices.length!==n||!p.linearIndices.every((i:unknown)=>typeof i==='number'&&Number.isSafeInteger(i)&&i>=0)||
        ['x','y','z'].some(axis=>!Array.isArray(p.coordinates?.[axis])||p.coordinates[axis].length!==n||!p.coordinates[axis].every(raw)))
      throw Error('Invalid slice worker response.');
    }else if(response.result.payload!==undefined)throw Error('Metadata worker returned an unexpected payload.');
    resolve(response.result as Metadata);
   }catch{reject(new PlotfileReadError('INVALID_RESPONSE','Metadata worker returned an invalid response.'));}
  });
 });
}


export function inspectPlotfileMetadataIsolated(path:string,options:{signal?:AbortSignal;timeoutMs?:number}={}){
 return readIsolated(path,options);
}
/** Same fixed worker/capacity/timeout as metadata; no browser execution settings. */
export function readPlotfileFieldSliceIsolated(path:string,request:PlotfileSliceRequest,options:{signal?:AbortSignal;timeoutMs?:number}={}){
 try{return readIsolated(path,options,copyPlotfileSliceRequest(request));}
 catch(error){return Promise.reject(error);}
}

export function readPlotfileOverviewIsolated(path:string,request:PlotfileOverviewRequest,options:{signal?:AbortSignal;timeoutMs?:number}={}){
 try{return readIsolated(path,options,undefined,copyOverviewRequest(request));}
 catch(error){return Promise.reject(error);}
}
