import {realpath,stat} from 'node:fs/promises';
export interface RunCheckpoint {path:string;filesystemIdentity:string}
/** Metadata identity only. Core owns HDF5 validity and continuation compatibility. */
export async function checkpointFilesystemIdentity(filename:string):Promise<string>{
 const resolved=await realpath(filename),info=await stat(resolved,{bigint:true});
 if(!info.isFile())throw new Error('Restart checkpoint is not a regular file.');
 if(await realpath(filename)!==resolved)throw new Error('Restart checkpoint target changed during inspection.');
 return JSON.stringify([resolved,...[info.dev,info.ino,info.size,info.mtimeNs,info.ctimeNs].map(String)]);
}
