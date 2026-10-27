/**
 * Recover the actual executable's link dependencies from recorded build evidence.
 * Parse and fingerprint stable inputs, then compare snapshots for freshness.
 * This is dependency provenance, not a second linker or an execution policy.
 */
import {constants} from 'node:fs';
import {open,realpath} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import type {LinkInputSnapshot} from '../src/host/contracts.ts';

const canonicalWindowsDrive=(value:string)=>/^[A-Za-z]:/.test(value)?value[0].toUpperCase()+value.slice(1):value;

/** Restricted Make depfile grammar: no variable expansion, recipes or wildcards. */
export function parseLinkDependencies(text:string,buildDirectory:string,executable:string,platform:NodeJS.Platform=process.platform):string[]{
 const windows=platform==='win32',paths=windows?path.win32:path.posix;
 if(!paths.isAbsolute(buildDirectory)||!paths.isAbsolute(executable)||text.includes('\0')||Buffer.byteLength(text)>8*1024*1024)
  throw new Error('Invalid linker dependency input.');
 const rules:{targets:string[];inputs:string[]}[]=[];
 let targets:string[]=[],inputs:string[]=[],token='',separator=false,driveColon=false;
 const flush=()=>{if(token){(separator?inputs:targets).push(token);token='';driveColon=false;}};
 const finish=()=>{flush();if(targets.length||inputs.length||separator){if(!separator||!targets.length)throw new Error('Malformed linker dependency rule.');rules.push({targets,inputs});}targets=[];inputs=[];separator=false;};
 for(let i=0;i<text.length;i++){
  const c=text[i];
  if(c==='\\'){
   const next=text[++i];if(next===undefined)throw new Error('Truncated linker dependency escape.');
   if(next==='\n')continue;
   if(next==='\r'&&text[i+1]==='\n'){i++;continue;}
   if(' \t\\#:$'.includes(next)){
    if(windows&&next===':')throw new Error('Ambiguous linker dependency rule.');
    token+=next;continue;
   }
   if(windows&&next.charCodeAt(0)>0x20&&next!=='\x7f'&&!'*?[];|'.includes(next)){
    token+='\\'+next;continue;
   }
   throw new Error('Unsupported linker dependency escape.');
  }else if(c==='$'){
   if(text[++i]!=='$')throw new Error('Variable expansion is not supported in linker dependencies.');
   token+='$';
  }else if(c==='#'){
   while(i+1<text.length&&text[i+1]!=='\n')i++;
  }else if(c===':'){
   if(windows&&!driveColon&&token.length===1&&/[A-Za-z]/.test(token)){
    const next=text[i+1];
    if(next!==undefined&&next!==' '&&next!=='\t'&&next!=='\r'&&next!=='\n'&&next!=='#'){
     if(next!=='/'&&next!=='\\')throw new Error('Drive-relative linker dependency path is not supported.');
     token+=':';driveColon=true;continue;
    }
   }
   flush();if(separator)throw new Error('Ambiguous linker dependency rule.');separator=true;
  }else if(c==='\n'){finish();}
  else if(c===' '||c==='\t'||c==='\r'){flush();}
  else{if('*?[];|'.includes(c))throw new Error('Unsupported linker dependency syntax.');token+=c;}
 }
 finish();
 const resolve=(p:string)=>windows?canonicalWindowsDrive(paths.resolve(buildDirectory,p)):paths.resolve(buildDirectory,p);
 if(!rules.length||rules[0].targets.length!==1||resolve(rules[0].targets[0])!==(windows?canonicalWindowsDrive(paths.resolve(executable)):paths.resolve(executable))||!rules[0].inputs.length)
  throw new Error('Linker dependency output does not match the expected executable.');
 const inputPaths=[...new Set(rules[0].inputs.map(resolve))].sort();
 if(inputPaths.length>20000)throw new Error('Linker input count exceeds budget.');
 const known=new Set(inputPaths);
 for(const rule of rules.slice(1))if(rule.inputs.length||rule.targets.length!==1||!known.has(resolve(rule.targets[0])))
  throw new Error('Unexpected additional linker dependency rule.');
 return inputPaths;
}
async function readStable(filePath:string,limit:number){
 const file=await open(filePath,constants.O_RDONLY|constants.O_NONBLOCK);
 try{
  const before=await file.stat();
  if(!before.isFile()||before.size>limit)throw new Error('Link input unavailable or oversized: '+filePath);
  const bytes=await file.readFile(),after=await file.stat();
  if(before.size!==after.size||before.mtimeMs!==after.mtimeMs||before.ctimeMs!==after.ctimeMs||bytes.length!==before.size)
   throw new Error('Link input changed during fingerprinting: '+filePath);
  return bytes;
 }finally{await file.close();}
}
export async function fingerprintLinkDependencies(buildDirectory:string,depfileRelative:string,executable:string):Promise<LinkInputSnapshot>{
 if(path.isAbsolute(depfileRelative)||depfileRelative.split('/').some(p=>p==='..'||!p)||depfileRelative.includes('\0'))
  throw new Error('Host-owned relative linker dependency file required.');
 const depfile=path.join(buildDirectory,depfileRelative);
 const raw=await readStable(depfile,8*1024*1024);
 const paths=parseLinkDependencies(raw.toString('utf8'),buildDirectory,executable);
 const files:LinkInputSnapshot['files']=[],unavailable:LinkInputSnapshot['unavailable']=[];
 let total=0;
 for(const input of paths){
  try{
   const resolvedPath=await realpath(input);
   const bytes=await readStable(resolvedPath,128*1024*1024);
   total+=bytes.length;if(total>512*1024*1024)throw new Error('Link input byte budget exceeded.');
   if(await realpath(input)!==resolvedPath)throw new Error('Link input target changed during fingerprinting.');
   files.push({path:input,resolvedPath,sha256:createHash('sha256').update(bytes).digest('hex'),size:bytes.length});
  }catch(e){
   if((e as NodeJS.ErrnoException).code!=='ENOENT')throw e;
   // Never infer that a missing path is harmless just because it resembles an LTO temporary.
   unavailable.push({path:input,reason:'missing'});
  }
 }
 return {kind:'linker-inputs',depfileSha256:createHash('sha256').update(raw).digest('hex'),files,unavailable};
}
export function changedLinkInputs(before:LinkInputSnapshot,after:LinkInputSnapshot):string[]{
 const old=new Map(before.files.map(f=>[f.path,f]));
 const changed=new Set<string>();
 for(const file of after.files){
  const previous=old.get(file.path);
  if(!previous||previous.sha256!==file.sha256||previous.size!==file.size||previous.resolvedPath!==file.resolvedPath)changed.add(file.path);
  old.delete(file.path);
 }
 for(const p of old.keys())changed.add(p);
 const a=new Set(before.unavailable.map(f=>f.path)),b=new Set(after.unavailable.map(f=>f.path));
 for(const p of a)if(!b.has(p))changed.add(p);
 for(const p of b)if(!a.has(p))changed.add(p);
 if(before.depfileSha256!==after.depfileSha256)changed.add('linker dependency graph');
 return [...changed];
}
