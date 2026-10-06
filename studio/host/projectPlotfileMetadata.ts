import {copyPointRequest} from '../src/host/plotfilePoint.ts';
import {copyOverviewRequest} from '../src/host/plotfileOverview.ts';
/** Project-scoped read audit, not a scientific result provider. */
import {stat} from 'node:fs/promises';
import {checkedPath,projectRoot} from './files.ts';
import {inspectPlotfileMetadataIsolated,readPlotfileFieldSliceIsolated,readPlotfileOverviewIsolated,readPlotfilePointIsolated} from './isolatedPlotfileMetadata.ts';
import {copyPlotfileSliceRequest} from './plotfileSliceRequest.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';

async function readScoped(root:string,projectId:string,request:unknown,mode:'metadata'|'slice'|'overview'|'point',signal?:AbortSignal){
 if(!request||typeof request!=='object'||Array.isArray(request))throw Error('Expected plotfile audit request.');
 const isSlice=mode==='slice',isOverview=mode==='overview',isPoint=mode==='point';
 const data=request as Record<string,unknown>,keys=isPoint?['projectId','relativePath','pointQuery','expectedFileSha256']:isOverview?['projectId','relativePath','overview','expectedFileSha256']:isSlice?['projectId','relativePath','slice','expectedFileSha256']:['projectId','relativePath'];
 if(Object.keys(data).length!==keys.length||Object.keys(data).some(k=>!keys.includes(k))||
    typeof data.projectId!=='string'||typeof data.relativePath!=='string')
  throw Error('Plotfile audit accepts only explicit project/file identity and bounded selection.');
 if(data.projectId!==projectId)throw Error('Project session changed.');
 // Copy request identity before any await; caller mutation cannot relabel results.
 const relativePath=data.relativePath,expected=data.expectedFileSha256;
 const point=isPoint?copyPointRequest(data.pointQuery):undefined;
 const overview=isOverview?copyOverviewRequest(data.overview):undefined;
 const slice=isSlice?copyPlotfileSliceRequest(data.slice):undefined;
 if((isSlice||isOverview||isPoint)&&(typeof expected!=='string'||!/^([a-f0-9]{64})$/.test(expected)))throw Error('Expected file SHA-256 is required.');
 if(await projectRoot(root)!==root)throw Error('Project root identity changed.');
 const target=await checkedPath(root,relativePath);
 const result=point?await readPlotfilePointIsolated(target,point,{signal}):overview?await readPlotfileOverviewIsolated(target,overview,{signal}):slice?await readPlotfileFieldSliceIsolated(target,slice,{signal}):await inspectPlotfileMetadataIsolated(target,{signal});
 if((isSlice||isOverview||isPoint)&&result.file.sha256!==expected)throw Error('Selected plotfile no longer matches requested file SHA-256.');
 // Pinning in the worker does not prove that the selected name still refers to it.
 const latest=await stat(await checkedPath(root,relativePath),{bigint:true});
 if(latest.dev.toString()!==result.file.device||latest.ino.toString()!==result.file.inode||
    latest.size!==BigInt(result.file.bytes)||latest.mtimeNs.toString()!==result.file.mtimeNs||latest.ctimeNs.toString()!==result.file.ctimeNs)
  throw Error('Selected plotfile changed during read audit.');
 return {protocolVersion:PROTOCOL_VERSION,projectId,relativePath,result};
}
export async function readProjectPlotfileMetadata(root:string,projectId:string,request:unknown,signal?:AbortSignal){
 const {result,...identity}=await readScoped(root,projectId,request,'metadata',signal);
 return {...identity,metadata:result};
}
export function readProjectPlotfileFieldSlice(root:string,projectId:string,request:unknown,signal?:AbortSignal){
 return readScoped(root,projectId,request,'slice',signal);
}

export function readProjectPlotfileOverview(root:string,projectId:string,request:unknown,signal?:AbortSignal){
 return readScoped(root,projectId,request,'overview',signal);
}

export function readProjectPlotfilePoint(root:string,projectId:string,request:unknown,signal?:AbortSignal){
 return readScoped(root,projectId,request,'point',signal);
}
