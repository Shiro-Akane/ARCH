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
