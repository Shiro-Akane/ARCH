import {constants} from 'node:fs';import {open,realpath,rename,unlink,link,stat} from 'node:fs/promises';import path from 'node:path';import {randomUUID} from 'node:crypto';
import {checkedPath} from './files.ts';import {ConfigError,configError,configPath,validateText,prepareSave,readConfig} from './config.ts';
import type {FileFingerprint} from '../src/host/contracts.ts';
// Fault injection is module-only for tests; never accepted by HTTP requests.
export interface AtomicHooks { beforeWrite?:()=>Promise<void>; publish?:(temporary:string,target:string)=>Promise<void> }
export async function atomicSave(root:string,relative:string,projectId:string,text:string,expected:FileFingerprint,hooks:AtomicHooks={}) {
 configPath(relative);validateText(text);await prepareSave(root,relative,projectId,expected,text);
 return publishConfig(root,relative,projectId,text,expected,false,hooks);
}
export async function publishConfig(root:string,relative:string,projectId:string,text:string,expected:FileFingerprint|undefined,createOnly:boolean,hooks:AtomicHooks={}) {
 configPath(relative);validateText(text);if(process.platform!=='linux')throw new ConfigError('write-failed','Local writes are currently supported in the documented WSL/Linux host only.');
 const parentRelative=path.dirname(relative);let directory;
 try{directory=parentRelative==='.'?await realpath(root):await checkedPath(root,parentRelative);if(directory!==root&&!directory.startsWith(root+path.sep))throw new ConfigError('outside-project-root','Destination directory is outside the project.');}catch(e){throw configError(e,relative);}
 const dir=await open(directory,constants.O_RDONLY|constants.O_DIRECTORY|constants.O_NOFOLLOW).catch(e=>{throw configError(e,relative);});
 const temporaryName=`.arch-config-${randomUUID()}.tmp`;const pinned=`/proc/self/fd/${dir.fd}`;const temporary=path.join(pinned,temporaryName);const target=path.join(pinned,path.basename(relative));let created=false;
 try {
  if(await realpath(pinned)!==directory)throw new ConfigError('outside-project-root','Destination directory identity changed.',relative);
  if(!createOnly)await prepareSave(root,relative,projectId,expected!,text);
  let mode=0o600;if(!createOnly)mode=(await stat(await checkedPath(root,relative))).mode&0o777;
  const file=await open(temporary,constants.O_CREAT|constants.O_EXCL|constants.O_WRONLY|constants.O_NOFOLLOW,mode);created=true;
  try{await file.chmod(mode);await hooks.beforeWrite?.();await file.writeFile(text,{encoding:'utf8'});await file.sync();}catch(e){throw e instanceof ConfigError?e:new ConfigError('write-failed','Writing temporary configuration failed; original preserved.',relative);}finally{await file.close();}
  if(await realpath(pinned)!==directory||(parentRelative==='.'?await realpath(root):await checkedPath(root,parentRelative))!==directory)throw new ConfigError('outside-project-root','Destination directory changed before save.',relative);
  if(!createOnly)await prepareSave(root,relative,projectId,expected!,text);
  try{if(hooks.publish)await hooks.publish(temporary,target);else if(createOnly)await link(temporary,target);else await rename(temporary,target);}catch(e){if((e as NodeJS.ErrnoException).code==='EEXIST')throw new ConfigError('destination-exists','Destination already exists. Choose another name.',relative);throw new ConfigError('rename-failed','Atomic publication failed; original preserved.',relative);}
  if(!createOnly)created=false;
  // Directory sync improves durability; content has already been atomically published.
  await dir.sync().catch(()=>undefined);
  const result=await readConfig(root,relative,projectId);if(result.text!==text)throw new ConfigError('changed-externally','File changed immediately after save. Reload explicitly to inspect disk.',relative,result.fingerprint);return result;
 } catch(e){throw configError(e,relative);}finally{if(created)await unlink(temporary).catch(()=>undefined);await dir.close();}
}
