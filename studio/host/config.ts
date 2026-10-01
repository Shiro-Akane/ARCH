import {constants} from 'node:fs';
import {open,realpath,stat,access} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {checkedPath,selectedPath} from './files.ts';
import type {ConfigFileError,ConfigFileErrorCode,ConfigReadResponse,FileFingerprint} from '../src/host/contracts.ts';
export const CONFIG_LIMIT=1024*1024;
export class ConfigError extends Error {
 readonly info:ConfigFileError;
 constructor(code:ConfigFileErrorCode,message:string,relativePath?:string,actual?:FileFingerprint){super(message);this.info={code,message,relativePath,actual};}
}
export function configPath(value:string):string {
 try{selectedPath(value);}catch{throw new ConfigError('invalid-path','Use a plain project-relative .par path.',value);}
 if(!value.endsWith('.par'))throw new ConfigError('invalid-path','Only .par configuration files are allowed.',value);return value;
}
export function configError(error:unknown,relativePath?:string):ConfigError {
 if(error instanceof ConfigError)return error;
 const code=(error as NodeJS.ErrnoException).code;
 if(code==='ENOENT')return new ConfigError('not-found','Configuration or its directory is missing.',relativePath);
 if(code==='EACCES'||code==='EPERM'||code==='EROFS')return new ConfigError('permission-denied','The configuration cannot be accessed with current permissions.',relativePath);
 if(code==='EEXIST')return new ConfigError('destination-exists','Destination already exists. Choose another name.',relativePath);
 return new ConfigError('read-error','Configuration inspection failed. Check the selected file and project path.',relativePath);
}
export function sameFingerprint(a:FileFingerprint,b:FileFingerprint){return a.sha256===b.sha256&&a.size===b.size&&a.modifiedTime===b.modifiedTime;}
export async function readConfig(root:string,relative:string|undefined,projectId:string):Promise<ConfigReadResponse> {
 if(relative===undefined)throw new ConfigError('not-found','No project configuration is selected.');configPath(relative);
 try {
  const target=await checkedPath(root,relative);const handle=await open(target,constants.O_RDONLY|constants.O_NOFOLLOW|constants.O_NONBLOCK);
  try {
   if(process.platform==='linux'&&await realpath(`/proc/self/fd/${handle.fd}`)!==target)throw new ConfigError('outside-project-root','Selected file identity changed.',relative);
   const before=await handle.stat();if(!before.isFile())throw new ConfigError('invalid-path','Configuration must be a regular file.',relative);if(before.size>CONFIG_LIMIT)throw new ConfigError('payload-too-large','Configuration exceeds 1 MiB.',relative);
   const chunks:Buffer[]=[];let size=0;while(true){const b=Buffer.alloc(65536);const {bytesRead}=await handle.read(b,0,b.length,null);if(!bytesRead)break;size+=bytesRead;if(size>CONFIG_LIMIT)throw new ConfigError('payload-too-large','Configuration exceeds 1 MiB.',relative);chunks.push(b.subarray(0,bytesRead));}
   const after=await handle.stat();const disk=await stat(await checkedPath(root,relative));
   if(size!==before.size||after.size!==before.size||after.mtimeMs!==before.mtimeMs||after.ctimeMs!==before.ctimeMs||disk.ino!==after.ino||disk.dev!==after.dev||disk.mtimeMs!==after.mtimeMs||disk.ctimeMs!==after.ctimeMs)throw new ConfigError('changed-externally','Configuration changed while reading. Retry explicitly.',relative);
   const bytes=Buffer.concat(chunks);let text:string;try{text=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(bytes);}catch{throw new ConfigError('read-error','Configuration must be valid UTF-8.',relative);}
   return {projectId,relativePath:relative,text,fingerprint:{sha256:createHash('sha256').update(bytes).digest('hex'),size,modifiedTime:after.mtime.toISOString()}};
  } finally {await handle.close();}
 } catch(error){throw configError(error,relative);}
}

export function validateText(text:unknown):asserts text is string {
 if(typeof text!=='string')throw new ConfigError('protocol-error','Configuration text is required.');
 if(Buffer.byteLength(text,'utf8')>CONFIG_LIMIT)throw new ConfigError('payload-too-large','Configuration exceeds 1 MiB.');
 if(Buffer.from(text,'utf8').toString('utf8')!==text)throw new ConfigError('protocol-error','Configuration contains invalid Unicode.');
}
export function validateFingerprint(value:unknown):asserts value is FileFingerprint {
 if(!value||typeof value!=='object')throw new ConfigError('protocol-error','Expected fingerprint is required.');
 const v=value as FileFingerprint;
 if(!/^[a-f0-9]{64}$/.test(v.sha256)||!Number.isSafeInteger(v.size)||v.size<0||typeof v.modifiedTime!=='string'||!Number.isFinite(Date.parse(v.modifiedTime)))throw new ConfigError('protocol-error','Expected fingerprint is invalid.');
}
export async function prepareSave(root:string,relative:string,projectId:string,expected:FileFingerprint,text:string){
 configPath(relative);validateText(text);validateFingerprint(expected);
 const disk=await readConfig(root,relative,projectId);
 if(!sameFingerprint(disk.fingerprint,expected))throw new ConfigError('changed-externally','This configuration changed on disk. Your working copy has been kept.',relative,disk.fingerprint);
 try{await access(await checkedPath(root,relative),constants.W_OK);}catch(e){throw configError(e,relative);}
 return disk;
}
