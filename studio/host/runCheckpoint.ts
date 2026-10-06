import {createHash} from 'node:crypto';
import {constants,type BigIntStats} from 'node:fs';
import fs from 'node:fs/promises';
export interface RunCheckpoint {path:string;filesystemIdentity:string}

/** Capture the opened regular file's metadata without reducing timestamp precision. */
function metadataIdentity(resolved:string,info:BigIntStats):string{
 return JSON.stringify([resolved,...[info.dev,info.ino,info.size,info.mtimeNs,info.ctimeNs].map(String)]);
}

/**
 * Bind Restart handoff to exact bytes as well as filesystem identity.
 * Workflow: resolve/open read-only -> stream SHA256 -> verify descriptor and final
 * path metadata -> close. An inode/timestamp reused after replacement must not
 * make changed bytes appear unchanged. Core owns HDF5 validity and continuation
 * compatibility; this check neither parses nor repairs checkpoint contents.
 */
export async function checkpointFilesystemIdentity(filename:string):Promise<string>{
 const resolved=await fs.realpath(filename);
 const handle=await fs.open(resolved,constants.O_RDONLY|constants.O_NOFOLLOW|constants.O_NONBLOCK);
 try{
  if(process.platform==='linux'&&await fs.realpath(`/proc/self/fd/${handle.fd}`)!==resolved)
   throw new Error('Restart checkpoint target changed during open.');
  const before=await handle.stat({bigint:true});
  if(!before.isFile())throw new Error('Restart checkpoint is not a regular file.');
  const beforeIdentity=metadataIdentity(resolved,before);
  const hash=createHash('sha256'),buffer=Buffer.alloc(65536);
  let bytes=0n;
  // Read only the observed extent with a fixed buffer, even if another writer
  // keeps appending. Metadata checks below reject any changed extent.
  while(bytes<before.size){
   const remaining=before.size-bytes;
   const length=remaining<BigInt(buffer.length)?Number(remaining):buffer.length;
   const result=await handle.read(buffer,0,length,null);
   if(!result.bytesRead)throw new Error('Restart checkpoint changed during inspection.');
   bytes+=BigInt(result.bytesRead);hash.update(buffer.subarray(0,result.bytesRead));
  }
  const after=await handle.stat({bigint:true});
  if(await fs.realpath(filename)!==resolved)
   throw new Error('Restart checkpoint target changed during inspection.');
  const latest=await fs.stat(resolved,{bigint:true});
  if(!after.isFile()||!latest.isFile()||metadataIdentity(resolved,after)!==beforeIdentity||
   metadataIdentity(resolved,latest)!==beforeIdentity)
   throw new Error('Restart checkpoint changed during inspection.');
  // The signature is opaque and versioned. Previously prepared metadata-only
  // signatures deliberately fail comparison rather than silently upgrading.
  return JSON.stringify(['checkpoint-content-v1',beforeIdentity,hash.digest('hex')]);
 }finally{await handle.close();}
}
