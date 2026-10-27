import {spawnOwned,terminateOwned} from './hostProcess.ts';
import type {WindowsHostTools} from './hostProcess.ts';
/**
 * Own a bounded subprocess for read-only HDF inspection.
 * Workflow: copy and validate a typed query, launch the fixed reader worker,
 * limit output/time, parse its result and validate the returned identity/shape.
 * Cancellation owns only that worker; no Core solver is invoked.
 */
import {copyPointRequest} from '../src/host/plotfilePoint.ts';
import type {PlotfilePointRequest} from '../src/host/plotfilePoint.ts';
import {copyOverviewRequest} from '../src/host/plotfileOverview.ts';
import type {PlotfileOverviewRequest} from '../src/host/plotfileOverview.ts';
import {validatePlotfileAudit,validatePlotfileOverview,validatePlotfilePoint} from '../src/host/plotfileAudit.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
import {record} from '../src/host/previewValidation.ts';
/** Reader isolation; publication/provenance use the shared client contract. */
import {fileURLToPath} from 'node:url';
import type {ChildProcess} from 'node:child_process';
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
// Bound the worker's serialized response to 64 KiB; large scientific arrays
// require a bounded typed selection rather than an unbounded metadata reply.
const OUTPUT_LIMIT=64*1024;

/**
 * Check the fixed worker's response with the same metadata, source/native
 * identities and raw-selection rules as the client. The local envelope only
 * adapts transport shape; it does not assert an external project association.
 * Formal publication completion is distinct from numerical qualification.
 */
export function validateIsolatedPlotfileResult(result:unknown,slice?:PlotfileSliceRequest,overview?:PlotfileOverviewRequest,point?:PlotfilePointRequest):Metadata{
 if(!record(result)||result.schemaVersion!==(point?'audit-point-1':overview?'audit-overview-1':slice?'audit-slice-1':'audit-1'))
  throw Error('Invalid metadata worker response.');
 // The producer never combines formal source evidence with a partial native
 // header. Keep that fail-closed distinction before the shared typed checks.
 if(record(result.candidateSourceIdentity)&&result.candidateSourceIdentity.version==='arch-plot-identity-1'&&
  (!record(result.candidateNativeGrid)||typeof result.candidateNativeGrid.version!=='string'||
   !result.candidateNativeGrid.version.startsWith('arch-native-')))
  throw Error('Formal publication lacks a formal native identity.');
 const projectId='isolated-reader',relativePath='selected-plotfile';
 const envelope={protocolVersion:PROTOCOL_VERSION,projectId,relativePath,metadata:result,result};
 const sha=record(result.file)&&typeof result.file.sha256==='string'?result.file.sha256:undefined;
 if(point){
  if(sha===undefined)throw Error('Missing native point file identity.');
  validatePlotfilePoint(envelope,projectId,relativePath,point,sha);
 }else if(overview){
  if(sha===undefined)throw Error('Missing overview file identity.');
  validatePlotfileOverview(envelope,projectId,relativePath,overview,sha);
 }else validatePlotfileAudit(envelope,projectId,relativePath,slice,sha);
 return result as unknown as Metadata;
}

/** Options are Host-owned. The browser must not supply execution settings. */
function readIsolated(path:string,options:{signal?:AbortSignal;timeoutMs?:number;windowsTools?:WindowsHostTools}={},slice?:PlotfileSliceRequest,overview?:PlotfileOverviewRequest,point?:PlotfilePointRequest):Promise<Metadata>{
 // Fifteen seconds is the maximum wall budget for this interactive read, in
 // milliseconds. Timeout is an explicit failure, never a partial success.
 const timeoutMs=options.timeoutMs??15_000;
 if(!Number.isSafeInteger(timeoutMs)||timeoutMs<1||timeoutMs>15_000)
  return Promise.reject(new RangeError('Metadata timeout must be 1..15000 ms.'));
 if(options.signal?.aborted)return Promise.reject(new PlotfileReadError('CANCELLED','Metadata read cancelled.'));
 if(active)return Promise.reject(new PlotfileReadError('BUSY','A metadata read is already active.'));
 active=true;
 return new Promise((resolve,reject)=>{
  let failure:Error|undefined;let bytes=0;const chunks:Buffer[]=[];
  // The heap cap is not a hard RSS/WASM cap. Process termination bounds lifetime.
  let child:ChildProcess;
  try {
   child=spawnOwned(process.execPath,['--max-old-space-size=256',fileURLToPath(new URL('./plotfileMetadataWorker.ts',import.meta.url)),...(process.platform==='win32'?['--native-tools',JSON.stringify(options.windowsTools)]:[]),path,...(point?[JSON.stringify({pointQuery:point})]:overview?[JSON.stringify({overview})]:slice?[JSON.stringify(slice)]:[])],
    {stdio:['ignore','pipe','ignore'],shell:false},options.windowsTools);
  } catch(error) {
   active=false;
   reject(new PlotfileReadError('WORKER_FAILED',error instanceof Error?error.message:'Metadata worker startup failed.'));
   return;
  }
  const terminate=(error:Error)=>{failure??=error;terminateOwned(child,'SIGKILL');};
  const cancel=()=>terminate(new PlotfileReadError('CANCELLED','Metadata read cancelled.'));
  const timer=setTimeout(()=>terminate(new PlotfileReadError('TIMEOUT','Metadata read exceeded its wall-clock budget.')),timeoutMs);
  options.signal?.addEventListener('abort',cancel,{once:true});
  // Covers cancellation between the initial check and listener registration.
  if(options.signal?.aborted)cancel();
  child.on('error',error=>{failure??=new PlotfileReadError('WORKER_FAILED',error.message);});
  child.stdout!.on('data',(chunk:Buffer)=>{
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
    resolve(validateIsolatedPlotfileResult(response.result,slice,overview,point));
   }catch{reject(new PlotfileReadError('INVALID_RESPONSE','Metadata worker returned an invalid response.'));}
  });
 });
}


export function inspectPlotfileMetadataIsolated(path:string,options:{signal?:AbortSignal;timeoutMs?:number;windowsTools?:WindowsHostTools}={}){
 return readIsolated(path,options);
}
/** Same fixed worker/capacity/timeout as metadata; no browser execution settings. */
export function readPlotfileFieldSliceIsolated(path:string,request:PlotfileSliceRequest,options:{signal?:AbortSignal;timeoutMs?:number;windowsTools?:WindowsHostTools}={}){
 try{return readIsolated(path,options,copyPlotfileSliceRequest(request));}
 catch(error){return Promise.reject(error);}
}

export function readPlotfileOverviewIsolated(path:string,request:PlotfileOverviewRequest,options:{signal?:AbortSignal;timeoutMs?:number;windowsTools?:WindowsHostTools}={}){
 try{return readIsolated(path,options,undefined,copyOverviewRequest(request));}
 catch(error){return Promise.reject(error);}
}

export function readPlotfilePointIsolated(path:string,request:PlotfilePointRequest,options:{signal?:AbortSignal;timeoutMs?:number;windowsTools?:WindowsHostTools}={}){
 try{return readIsolated(path,options,undefined,undefined,copyPointRequest(request));}
 catch(error){return Promise.reject(error);}
}
