import {open} from 'node:fs/promises';
import {constants} from 'node:fs';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
export interface NinjaDependency {object:string;inputs:string[]}
/** Read compiler-recorded include dependencies, not a source extension whitelist. */
export function parseNinjaDependencies(text:string,buildDirectory:string):NinjaDependency[]{
 if(!path.isAbsolute(buildDirectory)||Buffer.byteLength(text)>32*1024*1024)throw new Error('Invalid Ninja dependency input.');
 const result:NinjaDependency[]=[];let current:NinjaDependency|undefined,expected=0;
 const finish=()=>{if(current){if(current.inputs.length!==expected)throw new Error('Incomplete Ninja dependency record.');result.push(current);current=undefined;}};
 for(const line of text.split(/\r?\n/)){
  if(!line){finish();continue;}
  if(line.startsWith('    ')){
   if(!current||!line.slice(4)||line.includes('\0'))throw new Error('Malformed Ninja dependency path.');
   current.inputs.push(path.resolve(buildDirectory,line.slice(4)));continue;
  }
  finish();
  const match=/^(.+): #deps (\d+), deps mtime (\d+) \((VALID|STALE)\)$/.exec(line);
  if(!match||match[4]!=='VALID')throw new Error('Missing or stale compiler dependency record.');
  expected=Number(match[2]);if(!Number.isSafeInteger(expected)||expected<1||expected>100000)throw new Error('Invalid dependency count.');
  current={object:path.resolve(buildDirectory,match[1]),inputs:[]};
 }
 finish();if(!result.length)throw new Error('No compiler dependencies recorded.');
 return result;
}
export async function readNinjaDependencies(buildDirectory:string):Promise<NinjaDependency[]>{
 if(!path.isAbsolute(buildDirectory)||buildDirectory.includes('\0'))throw new Error('Host build directory required.');
 const {stdout}=await promisify(execFile)('/usr/bin/ninja',['-C',buildDirectory,'-t','deps'],
  {shell:false,encoding:'utf8',timeout:15000,maxBuffer:32*1024*1024,env:{PATH:'/usr/bin:/bin',LANG:'C.UTF-8'}});
 return parseNinjaDependencies(stdout,buildDirectory);
}

export async function fingerprintNinjaDependencies(buildDirectory:string){
 const objects=await readNinjaDependencies(buildDirectory);
 const paths=[...new Set(objects.flatMap(o=>o.inputs))].sort();
 if(paths.length>20000)throw new Error('Compiler input count exceeds budget.');
 const files:{path:string;sha256:string;size:number}[]=[];let total=0;
 for(const path of paths){
  const file=await open(path,constants.O_RDONLY|constants.O_NONBLOCK);
  try{
   const before=await file.stat();
   if(!before.isFile()||before.size>64*1024*1024)throw new Error('Compiler input unavailable or oversized: '+path);
   total+=before.size;if(total>512*1024*1024)throw new Error('Compiler input byte budget exceeded.');
   const bytes=await file.readFile(),after=await file.stat();
   if(before.size!==after.size||before.mtimeMs!==after.mtimeMs||bytes.length!==before.size)
    throw new Error('Compiler input changed during fingerprinting: '+path);
   files.push({path,sha256:createHash('sha256').update(bytes).digest('hex'),size:bytes.length});
  }finally{await file.close();}
 }
 return {kind:'ninja-compiler-inputs' as const,objectCount:objects.length,files};
}

export type CompilerInputSnapshot=Awaited<ReturnType<typeof fingerprintNinjaDependencies>>;
export function sameCompilerInputs(before:CompilerInputSnapshot|undefined,after:CompilerInputSnapshot|undefined):boolean{
 if(!before||!after||before.objectCount!==after.objectCount||before.files.length!==after.files.length)return false;
 const prior=new Map(before.files.map(f=>[f.path,f]));
 return after.files.every(f=>{const old=prior.get(f.path);return old?.sha256===f.sha256&&old.size===f.size;});
}
