/**
 * Reserve canonical output directories for cooperating Studio-managed runs.
 * Resolve aliases before taking advisory file locks and release acquired handles
 * on failure. Reservations coordinate owners; they never permit overwriting an
 * existing simulation or constrain unrelated external processes.
 */
import {spawn} from 'node:child_process';
import {mkdir,lstat,open,realpath} from 'node:fs/promises';
import {constants} from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
export interface RunOutputDirectory {path:string;canonicalPath:string}
/** Linux handles hold flock; Windows lock keys are opened by the native Job helper. */
export interface RunOutputReservation {readonly fd?:number;readonly lockKey?:string;close():Promise<void>}
function windowsCanonicalIdentity(value:string){
 let normalized=path.win32.normalize(value);
 if(/^\\\\\?\\UNC\\/i.test(normalized))normalized='\\\\'+normalized.slice(8);
 else if(/^\\\\\?\\/i.test(normalized))normalized=normalized.slice(4);
 if(normalized.length>3&&/[\\/]$/.test(normalized))normalized=normalized.slice(0,-1);
 // Win32 strips trailing dots/spaces in every ordinary path component.
 const root=path.win32.parse(normalized).root;
 const parts=normalized.slice(root.length).split('\\').map(part=>part.replace(/[ .]+$/,''));
 // Uppercase also unifies Greek final sigma; lowercasing alone underlocks it.
 return (root+parts.join('\\')).toUpperCase();
}
function canonicalIdentity(value:string){
 const normalized=path.normalize(value);
 // NTFS is normally case-insensitive. Folding shares reservations conservatively
 // for case aliases rather than underlocking two spellings of one output.
 return process.platform==='win32'?windowsCanonicalIdentity(normalized):normalized;
}
/** Resolve existing ancestors without creating scientific output directories. */
export async function canonicalOutputDirectory(target:string):Promise<string>{
 if(!path.isAbsolute(target)||target.includes('\0'))throw new Error('Invalid output directory identity.');
 let current=path.normalize(target);const suffix:string[]=[];
 while(true){
  try{return canonicalIdentity(path.join(await realpath(current),...suffix));}
  catch(e){
   if((e as NodeJS.ErrnoException).code!=='ENOENT')throw e;
   // A dangling symlink is not a missing directory.
   try{await lstat(current);throw new Error('Output directory contains an unresolved symbolic link.',{cause:e});}
   catch(inner){if((inner as NodeJS.ErrnoException).code!=='ENOENT')throw inner;}
   const parent=path.dirname(current);if(parent===current)throw e;
   suffix.unshift(path.basename(current));current=parent;
  }
 }
}
/** Stable, project-independent names; the helper acquires these before Core exists. */
export function windowsOutputLockKeys(directories:RunOutputDirectory[]):string[]{
 return [...new Set(directories.map(d=>createHash('sha256').update('arch-studio-run-output\0').update(windowsCanonicalIdentity(d.canonicalPath)).digest('hex')))].sort();
}
/** Linux advisory locks shared across Host/project sessions; never unlink lock files. */
export async function reserveRunOutputs(directories:RunOutputDirectory[]):Promise<RunOutputReservation[]>{
 if(process.platform==='win32'){
  for(const item of directories)if(await canonicalOutputDirectory(item.path)!==item.canonicalPath)
   throw new Error('Output directory changed after confirmation; prepare again.');
  // The native helper opens every key with an exclusive inheritable handle before
  // CreateProcess. A loser fails before Core starts; no output directory is made.
  const keys=windowsOutputLockKeys(directories);
  // Recheck non-existent ancestors after deriving names: a path alias or a newly
  // created component must not silently select a different lock namespace.
  for(const item of directories)if(await canonicalOutputDirectory(item.path)!==item.canonicalPath)
   throw new Error('Output directory changed during reservation; prepare again.');
  return keys.map(lockKey=>({lockKey,close:async()=>undefined}));
 }
 if(process.platform!=='linux')throw new Error('Unsupported Run output reservation platform.');
 const handles:RunOutputReservation[]=[];
 try{
  const uid=process.getuid!(),root='/tmp/arch-studio-run-outputs-'+uid;
  await mkdir(root,{mode:0o700}).catch((e:NodeJS.ErrnoException)=>{if(e.code!=='EEXIST')throw e;});
  const info=await lstat(root);
  if(!info.isDirectory()||info.uid!==uid||(info.mode&0o077)!==0)throw new Error('Unsafe output reservation directory.');
  for(const item of directories)if(await canonicalOutputDirectory(item.path)!==item.canonicalPath)
   throw new Error('Output directory changed after confirmation; prepare again.');
  for(const canonical of [...new Set(directories.map(d=>d.canonicalPath))].sort()){
   const lock=path.join(root,createHash('sha256').update(canonical).digest('hex'));
   const file=await open(lock,constants.O_CREAT|constants.O_RDWR|constants.O_NOFOLLOW,0o600);
   handles.push(file);const stat=await file.stat();
   if(!stat.isFile()||stat.uid!==uid||(stat.mode&0o077)!==0||stat.nlink!==1)throw new Error('Unsafe output reservation file.');
   await new Promise<void>((resolve,reject)=>{
    // fd 3 shares the parent's open-file description: the lock survives this
    // utility's exit, and is inherited by Core until the owned run has ended.
    const child=spawn('/usr/bin/flock',['--nonblock','3'],{shell:false,stdio:['ignore','ignore','pipe',file.fd]});
    let error='';child.stderr!.on('data',data=>{error+=String(data).slice(0,1024);});
    child.once('error',reject);child.once('close',code=>code===0?resolve():reject(new Error(code===1?
     'Output directory is reserved by another active Studio run: '+canonical:'Output reservation unavailable: '+error.trim())));
   });
  }
  // A directory created since confirmation resolves to the same canonical path.
  for(const item of directories)if(await canonicalOutputDirectory(item.path)!==item.canonicalPath)
   throw new Error('Output directory changed during reservation; prepare again.');
  return handles;
 }catch(e){await Promise.all(handles.map(h=>h.close()));throw e;}
}
