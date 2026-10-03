/** Project-scoped metadata audit, not a scientific result provider. */
import {stat} from 'node:fs/promises';
import {checkedPath,projectRoot} from './files.ts';
import {inspectPlotfileMetadataIsolated} from './isolatedPlotfileMetadata.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';

export async function readProjectPlotfileMetadata(root:string,projectId:string,request:unknown,signal?:AbortSignal){
 if(!request||typeof request!=='object'||Array.isArray(request))throw Error('Expected plotfile metadata request.');
 const data=request as Record<string,unknown>;
 if(Object.keys(data).length!==2||Object.keys(data).some(k=>!['projectId','relativePath'].includes(k))||
    typeof data.projectId!=='string'||typeof data.relativePath!=='string')
  throw Error('Metadata audit accepts only projectId and relativePath.');
 if(data.projectId!==projectId)throw Error('Project session changed.');
 if(await projectRoot(root)!==root)throw Error('Project root identity changed.');
 const target=await checkedPath(root,data.relativePath);
 const metadata=await inspectPlotfileMetadataIsolated(target,{signal});
 // The worker pins its descriptor. Also require the selected name still to refer
 // to that same file after it returns; replacement does not become a current result.
 const latest=await stat(await checkedPath(root,data.relativePath),{bigint:true});
 if(latest.dev.toString()!==metadata.file.device||latest.ino.toString()!==metadata.file.inode||
    latest.size!==BigInt(metadata.file.bytes)||latest.mtimeNs.toString()!==metadata.file.mtimeNs||latest.ctimeNs.toString()!==metadata.file.ctimeNs)
  throw Error('Selected plotfile changed during metadata audit.');
 return {protocolVersion:PROTOCOL_VERSION,projectId,relativePath:data.relativePath,metadata};
}
