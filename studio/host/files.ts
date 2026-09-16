import { constants } from 'node:fs';
import { lstat, open, realpath, stat } from 'node:fs/promises';
import path from 'node:path';
import { createHash } from 'node:crypto';
import type { ProjectFileRef } from '../src/host/contracts.ts';
export const MAX_FILE_BYTES = 64 * 1024 * 1024;
export function selectedPath(value: string): string {
  if (!value || value.includes('\\') || value.includes('%') || value.includes(':') || value.includes('\0') || path.isAbsolute(value) || value.split('/').some(x=>x==='..'||x==='.'||x==='')) throw new Error('Selected file must be a plain project-relative path without traversal');
  return value;
}
export async function projectRoot(value: string): Promise<string> {
  const root=await realpath(value);if (!(await stat(root)).isDirectory()) throw new Error('Project root is not a directory');return root;
}
// Refuse all selected-path symlinks, including inside-root links. This conservative policy
// avoids reading an escape target and keeps the same behavior across supported hosts.
async function checkedPath(root:string,relative:string):Promise<string> {
  selectedPath(relative);if (await realpath(root)!==root) throw new Error('Project root identity changed');
  let current=root;
  for (const part of relative.split('/')) {current=path.join(current,part);const s=await lstat(current);if(s.isSymbolicLink())throw new Error('Symlinks are not permitted for selected files');}
  const resolved=await realpath(current);
  if (path.relative(root,resolved).startsWith('..') || path.isAbsolute(path.relative(root,resolved))) throw new Error('Path outside project root');
  return resolved;
}
export async function fingerprint(root:string,relative:string,kind:ProjectFileRef['kind']):Promise<ProjectFileRef> {
  selectedPath(relative);
  try {
    const target=await checkedPath(root,relative);
    const handle=await open(target,constants.O_RDONLY|constants.O_NOFOLLOW|constants.O_NONBLOCK);
    try {
      // On Linux verify the opened descriptor too, before reading, to detect parent swaps.
      if (process.platform==='linux') {const actual=await realpath(`/proc/self/fd/${handle.fd}`);if(actual!==target)throw new Error('Selected file identity changed during open');}
      const before=await handle.stat();if(!before.isFile())throw new Error('Selected object is not a regular file');
      if(before.size>MAX_FILE_BYTES)throw new Error('Selected file exceeds 64 MiB fingerprint limit');
      const hash=createHash('sha256');const buffer=Buffer.alloc(65536);let bytes=0;
      while(true){const result=await handle.read(buffer,0,buffer.length,null);if(!result.bytesRead)break;bytes+=result.bytesRead;if(bytes>MAX_FILE_BYTES)throw new Error('File grew beyond fingerprint limit');hash.update(buffer.subarray(0,result.bytesRead));}
      const after=await handle.stat();const latest=await stat(await checkedPath(root,relative));
      if(bytes!==before.size||before.size!==after.size||before.mtimeMs!==after.mtimeMs||before.ctimeMs!==after.ctimeMs||latest.ino!==after.ino||latest.dev!==after.dev||latest.mtimeMs!==after.mtimeMs||latest.ctimeMs!==after.ctimeMs)throw new Error('File changed during refresh; retry');
      return {relativePath:relative,kind,exists:true,size:after.size,modifiedTime:after.mtime.toISOString(),sha256:hash.digest('hex'),changed:false};
    } finally {await handle.close();}
  } catch(error) {
    const code=(error as NodeJS.ErrnoException).code;
    if(code==='ENOENT')return {relativePath:relative,kind,exists:false,changed:false};
    const message=code==='EACCES'||code==='EPERM'?'Permission denied':error instanceof Error?error.message:'File inspection failed';
    return {relativePath:relative,kind,exists:false,changed:false,error:`${kind}: ${relative}: ${message}`};
  }
}
