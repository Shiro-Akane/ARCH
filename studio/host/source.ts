/**
 * Expose the selected source through the common project-file fingerprint reader.
 * This read-only adapter preserves project identity and never discovers, compiles
 * or executes a different source on behalf of the client.
 */
import {open,realpath,stat} from 'node:fs/promises';
import {constants,type BigIntStats} from 'node:fs';
import {createHash} from 'node:crypto';
import {checkedPath} from './files.ts';
import {inspect} from './buildManifest.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';

const MAX_SOURCE_BYTES=256*1024;
const READ_FLAGS=constants.O_RDONLY|constants.O_NOFOLLOW|constants.O_NONBLOCK;

interface SourceReadHooks {afterOpen?:()=>Promise<void>;afterRead?:()=>Promise<void>}
let hooks:SourceReadHooks={};

/** Module-only deterministic race injection for tests; never reachable from HTTP input. */
export function setSourceReadHooks(next:SourceReadHooks){hooks=next;}

function sameIdentity(a:BigIntStats,b:BigIntStats){
 return a.dev===b.dev&&a.ino===b.ino&&a.size===b.size&&a.mtimeNs===b.mtimeNs&&a.ctimeNs===b.ctimeNs;
}

/** Re-resolve every selected path component and bind it to the opened file identity. */
async function verifyCheckedPath(root:string,relativePath:string,target:string,expected:BigIntStats){
 const resolved=await checkedPath(root,relativePath);
 if(resolved!==target)throw new Error('Source identity changed.');
 const current=await stat(resolved,{bigint:true});
 if(!current.isFile()||!sameIdentity(current,expected))throw new Error('Source identity changed.');
}

/** Windows has no /proc descriptor link; reopen the checked path and compare identities. */
async function verifyReopenedPath(target:string,expected:BigIntStats){
 const reopened=await open(target,READ_FLAGS);
 try{
  const current=await reopened.stat({bigint:true});
  if(!current.isFile()||!sameIdentity(current,expected))throw new Error('Source identity changed.');
 }finally{await reopened.close();}
}

export async function readSource(root:string,relativePath:string|undefined,projectId:string){
 if(!relativePath)throw new Error('No selected source file.');
 const target=await checkedPath(root,relativePath);
 const initialPath=await stat(target,{bigint:true});
 if(!initialPath.isFile())throw new Error('Source identity changed.');
 if(initialPath.size>BigInt(MAX_SOURCE_BYTES))throw new Error('Selected source exceeds 256 KiB limit.');
 const initial=await inspect(root,relativePath);
 if(initial.size>MAX_SOURCE_BYTES)throw new Error('Selected source exceeds 256 KiB limit.');
 // inspect exposes millisecond precision; allow its Date rounding while all
 // handle/path identities below use exact nanosecond bigint statistics.
 const initialMtimeDelta=BigInt(Date.parse(initial.modifiedTime))*1_000_000n-initialPath.mtimeNs;
 if(BigInt(initial.size)!==initialPath.size||initialMtimeDelta<=-1_000_000n||initialMtimeDelta>=1_000_000n)
  throw new Error('Source changed while opening; retry.');
 const file=await open(target,READ_FLAGS);
 try{
  // Preserve the Linux descriptor check that detects a parent swap before open.
  if(process.platform==='linux'&&await realpath(`/proc/self/fd/${file.fd}`)!==target)
   throw new Error('Source identity changed.');
  const before=await file.stat({bigint:true});
  if(!before.isFile())throw new Error('Source identity changed.');
  if(before.size>BigInt(MAX_SOURCE_BYTES))throw new Error('Selected source exceeds 256 KiB limit.');
  if(!sameIdentity(before,initialPath))throw new Error('Source identity changed.');
  if(hooks.afterOpen)await hooks.afterOpen();
  // Reject a replacement between open and read. On Windows the reopen check
  // supplies the descriptor-to-path identity that /proc provides on Linux.
  await verifyCheckedPath(root,relativePath,target,before);
  if(process.platform==='win32')await verifyReopenedPath(target,before);
  const hash=createHash('sha256'),chunks:Buffer[]=[];let size=0;
  while(true){
   const chunk=Buffer.alloc(16384);
   const {bytesRead}=await file.read(chunk,0,chunk.length,null);
   if(!bytesRead)break;
   size+=bytesRead;
   if(size>MAX_SOURCE_BYTES)throw new Error('Source exceeds limit.');
   if(BigInt(size)>before.size)throw new Error('Source changed while reading; retry.');
   hash.update(chunk.subarray(0,bytesRead));
   chunks.push(chunk.subarray(0,bytesRead));
  }
  if(BigInt(size)!==before.size)throw new Error('Source changed while reading; retry.');
  if(hooks.afterRead)await hooks.afterRead();
  const after=await file.stat({bigint:true});
  if(!after.isFile()||!sameIdentity(after,before))throw new Error('Source changed while reading; retry.');
  // Check the checked path and (on Windows) a fresh handle after the read as well.
  await verifyCheckedPath(root,relativePath,target,before);
  if(process.platform==='win32')await verifyReopenedPath(target,before);
  const latest=await inspect(root,relativePath);
  const digest=hash.digest('hex');
  if(initial.sha256!==latest.sha256||initial.sha256!==digest||
   initial.size!==latest.size||initial.modifiedTime!==latest.modifiedTime||
   initialPath.size!==after.size)
   throw new Error('Source changed while reading; retry.');
  const final=await file.stat({bigint:true});
  if(!final.isFile()||!sameIdentity(final,before))throw new Error('Source changed while reading; retry.');
  await verifyCheckedPath(root,relativePath,target,before);
  if(process.platform==='win32')await verifyReopenedPath(target,before);
  return {protocolVersion:PROTOCOL_VERSION,projectId,relativePath,
   text:new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(Buffer.concat(chunks))};
 }finally{await file.close();}
}
