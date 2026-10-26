/**
 * Fingerprint actual compiler runtime libraries from ELF and cache evidence.
 * Use bounded tool output and stable file reads; unresolved evidence stays
 * unknown. These identities support build freshness, not a new runtime loader.
 */
import path from 'node:path';
import {windowsEnvironment} from './hostProcess.ts';
import type {WindowsHostTools} from './hostProcess.ts';
import {open,realpath} from 'node:fs/promises';
import {constants} from 'node:fs';
import {createHash} from 'node:crypto';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import type {CMakeConfigurationEvidence} from './cmakeEvidence.ts';
import type {BuildManifest} from '../src/host/contracts.ts';

export interface RuntimeFile {path:string;resolvedPath:string;sha256:string;size:number}
export interface ElfToolRuntimeEvidence {
 kind:'static-tool-runtime';version:1;dependenciesComplete:false;
 roots:string[];inspectors:RuntimeFile[];loaderCache:RuntimeFile;
 nodes:(RuntimeFile&{needed:string[];interpreter:string|null;searchPaths:string[]})[];
 edges:{parent:string;requested:string;candidate:string;kind:'interpreter'|'DT_NEEDED'}[];
 unresolved:{parent:string;requested:string;reason:'unmodeled-rpath'|'missing-or-ambiguous-cache-candidate';candidates:string[]}[];
}
export interface WindowsToolRuntimeEvidence {
 kind:'static-tool-runtime';version:2;platform:'windows-x86_64';dependenciesComplete:false;
 roots:string[];inspectors:RuntimeFile[];
 searchInputs:{applicationDirectories:string[];hostPath:string[];systemDirectory:string};
 searchInputsSha256:string;
 nodes:(RuntimeFile&{format:'PE32+';machine:'AMD64';imports:string[];delayImportState:'observed'|'unparsed';delayImports?:string[]})[];
 edges:{parent:string;requested:string;kind:'PE_IMPORT'|'PE_DELAY_IMPORT';resolution:'selected-prefix-file'|'windows-system-boundary'|'api-set-boundary'|'unresolved';candidates:RuntimeFile[];resolvedPath?:string}[];
 unresolved:{parent:string;requested:string;reason:'missing'|'ambiguous'|'system-boundary'|'api-set-boundary'|'delay-imports-unparsed';candidates:string[]}[];
 limitations:('dynamic-loads-unmodeled'|'windows-loader-order-not-modeled')[];
}
export type ToolRuntimeEvidence=ElfToolRuntimeEvidence|WindowsToolRuntimeEvidence;
const READELF='/usr/bin/readelf',LDCONFIG='/usr/sbin/ldconfig';
type Capture=(program:string,args:string[])=>Promise<string>;
const capture:Capture=async(program,args)=>(await promisify(execFile)(program,args,{env:{PATH:'/usr/bin:/bin',LC_ALL:'C'},timeout:10000,maxBuffer:1024*1024})).stdout;
function absolute(s:string){return path.isAbsolute(s)&&!s.includes('\0');}
export async function runtimeFingerprint(file:string):Promise<RuntimeFile>{
 if(!absolute(file))throw new Error('Absolute runtime evidence path required.');
 const resolvedPath=await realpath(file);
 const f=await open(resolvedPath,constants.O_RDONLY|constants.O_NOFOLLOW|constants.O_NONBLOCK);
 try{
  const before=await f.stat();
  if(!before.isFile()||before.size>128*1024*1024)throw new Error('Runtime input unavailable or over budget.');
  const hash=createHash('sha256'),buffer=Buffer.alloc(1024*1024);let size=0;
  for(;;){const {bytesRead}=await f.read(buffer,0,buffer.length,null);if(!bytesRead)break;size+=bytesRead;if(size>128*1024*1024)throw new Error('Runtime input exceeds budget.');hash.update(buffer.subarray(0,bytesRead));}
  const after=await f.stat();
  if(before.dev!==after.dev||before.ino!==after.ino||before.size!==after.size||before.mtimeMs!==after.mtimeMs||before.ctimeMs!==after.ctimeMs||await realpath(file)!==resolvedPath)throw new Error('Runtime input changed while reading.');
  const check=await open(resolvedPath,constants.O_RDONLY|constants.O_NOFOLLOW|constants.O_NONBLOCK);
  try{const now=await check.stat();if(now.dev!==before.dev||now.ino!==before.ino||now.size!==before.size||now.mtimeMs!==before.mtimeMs||now.ctimeMs!==before.ctimeMs)throw new Error('Runtime path changed while reading.');}finally{await check.close();}
  return {path:file,resolvedPath,sha256:hash.digest('hex'),size};
 }finally{await f.close();}
}
export function parseRuntimeElf(text:string){
 if(!/Class:\s+ELF64\b/.test(text)||!/Machine:\s+Advanced Micro Devices X86-64\b/.test(text))throw new Error('Only Linux x86-64 ELF runtime evidence is covered.');
 const needed=[...text.matchAll(/\(NEEDED\)\s+Shared library: \[([^\]]*)\]/g)].map(m=>m[1]);
 const interpreters=[...text.matchAll(/\[Requesting program interpreter: ([^\]]+)\]/g)].map(m=>m[1]);
 const searchPaths=[...text.matchAll(/\((?:RPATH|RUNPATH)\)\s+.*?\[([^\]]*)\]/g)].map(m=>m[1]);
 if(needed.length>128||new Set(needed).size!==needed.length||needed.some(n=>!n||n.includes('/')||n.includes('\0'))||interpreters.length>1||interpreters.some(s=>!absolute(s)))throw new Error('Malformed ELF runtime declaration.');
 return {needed,interpreter:interpreters[0]??null,searchPaths};
}
export function parseRuntimeCache(text:string){
 const cache=new Map<string,Set<string>>();
 for(const line of text.split('\n')){
  const m=/^\s*(\S+) \(([^)]*)\) => (\/.+?)\s*$/.exec(line);
  if(m&&m[2].split(',').includes('x86-64')){const set=cache.get(m[1])??new Set<string>();set.add(m[3]);cache.set(m[1],set);}
 }
 return cache;
}
/** Hooks are module-only negative-test seams; HTTP never supplies commands/paths. */
export async function readToolRuntimeEvidence(roots:string[],probe:Capture=capture,tools?:WindowsHostTools):Promise<ToolRuntimeEvidence>{
 if(process.platform==='win32')return readWindowsToolRuntimeEvidence(roots,tools,probe===capture?undefined:probe);
 roots=[...new Set(roots)].sort();if(!roots.length||roots.length>32||roots.some(s=>!absolute(s)))throw new Error('Runtime root budget or path invalid.');
 const inspectors=await Promise.all([READELF,LDCONFIG].map(runtimeFingerprint)),loaderCache=await runtimeFingerprint('/etc/ld.so.cache');
 const cache=parseRuntimeCache(await probe(LDCONFIG,['-p']));
 const nodes:ElfToolRuntimeEvidence['nodes']=[],edges:ElfToolRuntimeEvidence['edges']=[],unresolved:ElfToolRuntimeEvidence['unresolved']=[];
 const seen=new Set<string>(),pending=[...roots];
 while(pending.length){
  const file=pending.shift()!,before=await runtimeFingerprint(file);
  if(seen.has(before.resolvedPath))continue;
  if(nodes.length>=256)throw new Error('Runtime graph exceeds node budget.');
  const details=parseRuntimeElf(await probe(READELF,['-h','-l','-d','--',before.resolvedPath]));
  if(JSON.stringify(before)!==JSON.stringify(await runtimeFingerprint(file)))throw new Error('Runtime ELF changed during inspection.');
  seen.add(before.resolvedPath);nodes.push({...before,...details});
  if(details.interpreter){edges.push({parent:before.resolvedPath,requested:details.interpreter,candidate:details.interpreter,kind:'interpreter'});pending.push(details.interpreter);}
  for(const requested of details.needed){
   const candidates=[...(cache.get(requested)??[])].sort();
   if(details.searchPaths.length||candidates.length!==1)unresolved.push({parent:before.resolvedPath,requested,reason:details.searchPaths.length?'unmodeled-rpath':'missing-or-ambiguous-cache-candidate',candidates});
   else{edges.push({parent:before.resolvedPath,requested,candidate:candidates[0],kind:'DT_NEEDED'});pending.push(candidates[0]);}
  }
  if(edges.length+unresolved.length>8192)throw new Error('Runtime edge budget exceeded.');
 }
 for(const f of [...nodes,...inspectors,loaderCache])if(!sameRuntimeFile(f,await runtimeFingerprint(f.path)))throw new Error('Runtime graph changed before completion.');
 return {kind:'static-tool-runtime',version:1,dependenciesComplete:false,roots,inspectors,loaderCache,nodes:nodes.sort((a,b)=>a.resolvedPath.localeCompare(b.resolvedPath)),edges,unresolved};
}
export function toolRuntimeRoots(configuration:CMakeConfigurationEvidence|undefined,drivers:BuildManifest['compilerDrivers'],tools?:WindowsHostTools){
 if(!configuration?.generatorTools?.length||!drivers?.length)throw new Error('Runtime root identities unavailable.');
 return [...configuration.generatorTools.map(t=>t.path),...drivers.flatMap(d=>[d.path,...(d.components??[]).filter(c=>c.role!=='specs').map(c=>c.path)]),...(tools?[tools.supervisor,...(tools.publisher?[tools.publisher]:[])]:[])];
}
function sameRuntimeFile(a:RuntimeFile,b:RuntimeFile){return a.path===b.path&&a.resolvedPath===b.resolvedPath&&a.sha256===b.sha256&&a.size===b.size;}
export function changedRuntimeInputs(a:ToolRuntimeEvidence,b:ToolRuntimeEvidence){
 const prior=new Map(runtimeEvidenceFiles(a).map(f=>[f.path,f]));
 const changed:string[]=[];
 for(const f of runtimeEvidenceFiles(b)){const old=prior.get(f.path);if(!old||!sameRuntimeFile(old,f))changed.push(f.path);prior.delete(f.path);}
 changed.push(...prior.keys());
 if(a.version!==b.version||(a.version===2&&b.version===2&&(a.searchInputsSha256!==b.searchInputsSha256||JSON.stringify(a.nodes)!==JSON.stringify(b.nodes)||JSON.stringify(a.limitations)!==JSON.stringify(b.limitations)))||JSON.stringify(a.roots)!==JSON.stringify(b.roots)||JSON.stringify(a.edges)!==JSON.stringify(b.edges)||JSON.stringify(a.unresolved)!==JSON.stringify(b.unresolved))changed.push('static tool runtime dependency graph');
 return [...new Set(changed)];
}
export function sameRuntimeInputs(a:ToolRuntimeEvidence|undefined,b:ToolRuntimeEvidence|undefined){return !!a&&!!b&&changedRuntimeInputs(a,b).length===0;}
export function validRuntimeEvidence(value:unknown):value is ToolRuntimeEvidence{
 if(!value||typeof value!=='object')return false;
 if((value as {version?:unknown}).version===2)return validWindowsRuntimeEvidence(value);
 const c=value as ElfToolRuntimeEvidence;
 const file=(v:RuntimeFile)=>v&&typeof v.path==='string'&&absolute(v.path)&&typeof v.resolvedPath==='string'&&absolute(v.resolvedPath)&&/^[a-f0-9]{64}$/.test(v.sha256)&&Number.isSafeInteger(v.size)&&v.size>=0&&v.size<=128*1024*1024;
 const text=(v:unknown)=>typeof v==='string'&&v.length<=4096&&!v.includes('\0');
 if(c.kind!=='static-tool-runtime'||c.version!==1||c.dependenciesComplete!==false||!Array.isArray(c.roots)||!c.roots.length||c.roots.length>32||c.roots.some(s=>!text(s)||!absolute(s))||new Set(c.roots).size!==c.roots.length||!Array.isArray(c.inspectors)||c.inspectors.length!==2||!file(c.loaderCache)||c.loaderCache.path!=='/etc/ld.so.cache'||!Array.isArray(c.nodes)||!c.nodes.length||c.nodes.length>256||!Array.isArray(c.edges)||!Array.isArray(c.unresolved)||c.edges.length+c.unresolved.length>8192)return false;
 if(c.inspectors.some(f=>!file(f))||c.inspectors[0].path!==READELF||c.inspectors[1].path!==LDCONFIG)return false;
 const resolved=new Set<string>();
 for(const n of c.nodes){if(!file(n)||resolved.has(n.resolvedPath)||!Array.isArray(n.needed)||n.needed.length>128||n.needed.some(s=>!text(s)||!s||s.includes('/'))||new Set(n.needed).size!==n.needed.length||!Array.isArray(n.searchPaths)||n.searchPaths.some(s=>!text(s))||(n.interpreter!==null&&(!text(n.interpreter)||!absolute(n.interpreter))))return false;resolved.add(n.resolvedPath);}
 for(const e of c.edges)if(!e||!resolved.has(e.parent)||!text(e.requested)||!e.requested||!text(e.candidate)||!absolute(e.candidate)||!['interpreter','DT_NEEDED'].includes(e.kind))return false;
 for(const e of c.unresolved)if(!e||!resolved.has(e.parent)||!text(e.requested)||!['unmodeled-rpath','missing-or-ambiguous-cache-candidate'].includes(e.reason)||!Array.isArray(e.candidates)||e.candidates.length>256||e.candidates.some(s=>!text(s)||!absolute(s)))return false;
 return true;
}

/** File identities include every observed PE candidate, including explicit system boundaries. */
export function runtimeEvidenceFiles(evidence:ToolRuntimeEvidence):RuntimeFile[]{
 return evidence.version===1?[...evidence.nodes,...evidence.inspectors,evidence.loaderCache]:
  [...new Map([...evidence.nodes,...evidence.inspectors,...evidence.edges.flatMap(e=>e.candidates)].map(f=>[JSON.stringify([f.path,f.resolvedPath,f.sha256,f.size]),f])).values()];
}
// GNU cc1plus -p emits 35.6 MB of unwind tables as well as imports. Keep
// that measured PE output bounded separately; the ELF 1 MiB limit is unchanged.
const PE_OUTPUT_LIMIT=64*1024*1024,PE_TOTAL_OUTPUT_LIMIT=256*1024*1024;
const peName=(s:string)=>/^[a-zA-Z0-9_+.-]+\.dll$/i.test(s)&&s.length<=256;
export function parseRuntimePe(text:string){
 if(Buffer.byteLength(text)>PE_OUTPUT_LIMIT||!/^.+file format pei-x86-64\s*$/m.test(text)||!/^Magic\s+020b\s+\(PE32\+\)/m.test(text))throw new Error('Only Windows AMD64 PE32+ runtime evidence is covered.');
 const delays=[...text.matchAll(/^Entry d\s+([0-9a-f]+)\s+([0-9a-f]+)\s+Delay Import Directory\s*$/gm)];
 if(delays.length!==1)throw new Error('PE delay-import directory unavailable.');
 // objdump emits DLL Name in the ordinary import table. Stop before a possible
 // delay table; its presence remains explicitly unparsed until separately covered.
 const table=text.split(/The Delay Import/i)[0];
 const imports=[...table.matchAll(/^\s*DLL Name: ([^\r\n]+)\s*$/gm)].map(m=>m[1].trim());
 if(imports.length>128||imports.some(n=>!peName(n))||new Set(imports.map(n=>n.toLowerCase())).size!==imports.length)throw new Error('Malformed PE import declaration.');
 const observed=/^0+$/.test(delays[0][1])&&/^0+$/.test(delays[0][2]);
 return {format:'PE32+' as const,machine:'AMD64' as const,imports,
  delayImportState:observed?'observed' as const:'unparsed' as const,...(observed?{delayImports:[] as string[]}: {})};
}
async function readWindowsToolRuntimeEvidence(roots:string[],tools?:WindowsHostTools,probe?:Capture):Promise<WindowsToolRuntimeEvidence>{
 if(!tools?.objdump)throw new Error('Trusted native PE inspector required.');
 const env=windowsEnvironment(tools);
 roots=[...new Set(roots)].sort();
 if(!roots.length||roots.length>32||roots.some(s=>!absolute(s)))throw new Error('Runtime root budget or path invalid.');
 const inspectors=[await runtimeFingerprint(tools.objdump)];
 const searchInputs={applicationDirectories:[...new Set(roots.map(s=>path.dirname(s)))].sort(),
  hostPath:tools.path.split(path.delimiter),systemDirectory:path.join(env.SystemRoot??env.WINDIR??'', 'System32')};
 if(searchInputs.hostPath.length>256||[...searchInputs.applicationDirectories,...searchInputs.hostPath,searchInputs.systemDirectory].some(s=>!absolute(s)))throw new Error('Absolute native runtime search inputs required.');
 const searchInputsSha256=createHash('sha256').update(JSON.stringify(searchInputs)).digest('hex');
 const inspect=probe??(async(program:string,args:string[])=>(await promisify(execFile)(program,args,{env,timeout:10000,maxBuffer:PE_OUTPUT_LIMIT,windowsHide:true})).stdout);
 const nodes:WindowsToolRuntimeEvidence['nodes']=[],edges:WindowsToolRuntimeEvidence['edges']=[],unresolved:WindowsToolRuntimeEvidence['unresolved']=[];
 const pending=[...roots],seen=new Set<string>();let inspectedBytes=0;
 while(pending.length){
  const file=pending.shift()!,before=await runtimeFingerprint(file);
  if(seen.has(before.resolvedPath))continue;
  if(nodes.length>=256)throw new Error('Runtime graph exceeds node budget.');
  const text=await inspect(tools.objdump,['-p','--',before.resolvedPath]);
  inspectedBytes+=Buffer.byteLength(text);
  if(inspectedBytes>PE_TOTAL_OUTPUT_LIMIT)throw new Error('PE inspector total output budget exceeded.');
  const details=parseRuntimePe(text);
  if(!sameRuntimeFile(before,await runtimeFingerprint(file)))throw new Error('Runtime PE changed during inspection.');
  seen.add(before.resolvedPath);nodes.push({...before,...details});
  if(details.delayImportState==='unparsed')unresolved.push({parent:before.resolvedPath,requested:'delay-import-directory',reason:'delay-imports-unparsed',candidates:[]});
  for(const requested of details.imports){
   const candidates:RuntimeFile[]=[];
   const dirs=[...new Set([path.dirname(before.resolvedPath),...searchInputs.applicationDirectories,...searchInputs.hostPath,searchInputs.systemDirectory])];
   const seenCandidates=new Set<string>();
   for(const dir of dirs){
    try{const f=await runtimeFingerprint(path.join(dir,requested));if(!seenCandidates.has(f.resolvedPath)){seenCandidates.add(f.resolvedPath);candidates.push(f);if(candidates.length>256)throw new Error('Runtime candidate budget exceeded.');}}
    catch(e){if((e as NodeJS.ErrnoException).code!=='ENOENT')throw e;}
   }
   const api=/^(?:api|ext)-ms-/i.test(requested),system=candidates.some(f=>path.dirname(f.resolvedPath).toLowerCase()===searchInputs.systemDirectory.toLowerCase());
   const resolution=api?'api-set-boundary':system?'windows-system-boundary':candidates.length===1?'selected-prefix-file':'unresolved';
   const selected=resolution==='selected-prefix-file'?candidates[0]:undefined;
   edges.push({parent:before.resolvedPath,requested,kind:'PE_IMPORT',resolution,candidates,...(selected?{resolvedPath:selected.resolvedPath}:{})});
   if(selected)pending.push(selected.path);
   else unresolved.push({parent:before.resolvedPath,requested,reason:api?'api-set-boundary':system?'system-boundary':candidates.length?'ambiguous':'missing',candidates:candidates.map(f=>f.path)});
   if(edges.length+unresolved.length>8192)throw new Error('Runtime edge budget exceeded.');
  }
 }
 const result:WindowsToolRuntimeEvidence={kind:'static-tool-runtime',version:2,platform:'windows-x86_64',dependenciesComplete:false,roots,inspectors,searchInputs,searchInputsSha256,nodes:nodes.sort((a,b)=>a.resolvedPath.localeCompare(b.resolvedPath)),edges,unresolved,limitations:['dynamic-loads-unmodeled','windows-loader-order-not-modeled']};
 for(const f of runtimeEvidenceFiles(result))if(!sameRuntimeFile(f,await runtimeFingerprint(f.path)))throw new Error('Runtime graph changed before completion.');
 return result;
}
function validWindowsRuntimeEvidence(value:unknown):value is WindowsToolRuntimeEvidence{
 const c=value as WindowsToolRuntimeEvidence;
 const text=(s:unknown):s is string=>typeof s==='string'&&s.length>0&&s.length<=4096&&!s.includes('\0');
 const absolute=(s:unknown):s is string=>text(s)&&/^[A-Za-z]:[\\/]/.test(s)&&path.win32.isAbsolute(s);
 const sha=(s:unknown)=>typeof s==='string'&&/^[a-f0-9]{64}$/.test(s);
 const file=(f:RuntimeFile)=>!!f&&absolute(f.path)&&absolute(f.resolvedPath)&&sha(f.sha256)&&
  Number.isSafeInteger(f.size)&&f.size>=0&&f.size<=128*1024*1024;
 const paths=(v:unknown,max:number,unique=true)=>Array.isArray(v)&&v.length<=max&&
  v.every(absolute)&&(!unique||new Set(v).size===v.length);
 const names=(v:unknown)=>Array.isArray(v)&&v.length<=128&&
  v.every(s=>typeof s==='string'&&peName(s))&&new Set(v.map(s=>s.toLowerCase())).size===v.length;

 if(!c||c.kind!=='static-tool-runtime'||c.version!==2||c.platform!=='windows-x86_64'||c.dependenciesComplete!==false)return false;
 if(!paths(c.roots,32)||!c.roots.length)return false;
 if(!Array.isArray(c.inspectors)||c.inspectors.length!==1||!file(c.inspectors[0]))return false;
 const search=c.searchInputs;
 if(!search||!paths(search.applicationDirectories,32)||!paths(search.hostPath,256,false)||!absolute(search.systemDirectory))return false;
 if(!sha(c.searchInputsSha256)||c.searchInputsSha256!==createHash('sha256').update(JSON.stringify(search)).digest('hex'))return false;
 if(!Array.isArray(c.nodes)||!c.nodes.length||c.nodes.length>256)return false;
 if(!Array.isArray(c.edges)||!Array.isArray(c.unresolved)||c.edges.length+c.unresolved.length>8192)return false;
 if(JSON.stringify(c.limitations)!==JSON.stringify(['dynamic-loads-unmodeled','windows-loader-order-not-modeled']))return false;

 const nodes=new Map<string,WindowsToolRuntimeEvidence['nodes'][number]>();
 for(const n of c.nodes){
  if(!file(n)||nodes.has(n.resolvedPath)||n.format!=='PE32+'||n.machine!=='AMD64')return false;
  if(!names(n.imports)||!['observed','unparsed'].includes(n.delayImportState))return false;
  if(n.delayImportState==='observed'?!names(n.delayImports):n.delayImports!==undefined)return false;
  nodes.set(n.resolvedPath,n);
 }
 if(c.roots.some(root=>!c.nodes.some(n=>n.path===root||n.resolvedPath===root)))return false;

 const edgeKeys=new Set<string>();
 for(const e of c.edges){
  if(!e||!nodes.has(e.parent)||!peName(e.requested))return false;
  if(!['PE_IMPORT','PE_DELAY_IMPORT'].includes(e.kind))return false;
  if(!['selected-prefix-file','windows-system-boundary','api-set-boundary','unresolved'].includes(e.resolution))return false;
  if(!Array.isArray(e.candidates)||e.candidates.length>256||e.candidates.some(f=>!file(f)))return false;
  if(new Set(e.candidates.map(f=>f.resolvedPath)).size!==e.candidates.length)return false;
  const key=JSON.stringify([e.parent,e.kind,e.requested]);
  if(edgeKeys.has(key))return false;
  edgeKeys.add(key);
  const parent=nodes.get(e.parent)!;
  if(!(e.kind==='PE_IMPORT'?parent.imports:parent.delayImports??[]).includes(e.requested))return false;
  if(e.resolution==='selected-prefix-file'){
   const n=nodes.get(e.resolvedPath??''),candidate=e.candidates[0];
   // Windows spelling aliases may have different requested paths. The actual
   // resolved file and its byte identity must still agree.
   if(e.candidates.length!==1||!n||n.resolvedPath!==candidate.resolvedPath||
      n.sha256!==candidate.sha256||n.size!==candidate.size)return false;
  }else if(e.resolvedPath!==undefined)return false;
 }
 for(const n of c.nodes){
  for(const requested of n.imports)if(!edgeKeys.has(JSON.stringify([n.resolvedPath,'PE_IMPORT',requested])))return false;
  for(const requested of n.delayImports??[])if(!edgeKeys.has(JSON.stringify([n.resolvedPath,'PE_DELAY_IMPORT',requested])))return false;
 }

 const unresolvedKeys=new Set<string>();
 for(const u of c.unresolved){
  if(!u||!nodes.has(u.parent)||!text(u.requested)||!paths(u.candidates,256))return false;
  if(!['missing','ambiguous','system-boundary','api-set-boundary','delay-imports-unparsed'].includes(u.reason))return false;
  const key=JSON.stringify([u.parent,u.requested]);
  if(unresolvedKeys.has(key))return false;
  unresolvedKeys.add(key);
  if(u.reason==='delay-imports-unparsed'){
   if(u.requested!=='delay-import-directory'||u.candidates.length||nodes.get(u.parent)?.delayImportState!=='unparsed')return false;
  }else{
   const e=c.edges.find(e=>e.parent===u.parent&&e.requested===u.requested);
   if(!e||e.resolution==='selected-prefix-file')return false;
   if(JSON.stringify(e.candidates.map(f=>f.path))!==JSON.stringify(u.candidates))return false;
   const reason=e.resolution==='windows-system-boundary'?'system-boundary':
    e.resolution==='api-set-boundary'?'api-set-boundary':e.candidates.length?'ambiguous':'missing';
   if(u.reason!==reason)return false;
  }
 }
 for(const e of c.edges)if(e.resolution!=='selected-prefix-file'&&!unresolvedKeys.has(JSON.stringify([e.parent,e.requested])))return false;
 for(const n of c.nodes)if(n.delayImportState==='unparsed'&&!unresolvedKeys.has(JSON.stringify([n.resolvedPath,'delay-import-directory'])))return false;

 // Preserve every observed identity. A later candidate must never hide an
 // earlier node or inspector with the same path and different bytes.
 const byPath=new Map<string,RuntimeFile>(),byResolved=new Map<string,RuntimeFile>();
 for(const f of runtimeEvidenceFiles(c)){
  const prior=byPath.get(f.path),resolved=byResolved.get(f.resolvedPath);
  if(prior&&!sameRuntimeFile(prior,f))return false;
  if(resolved&&(resolved.sha256!==f.sha256||resolved.size!==f.size))return false;
  byPath.set(f.path,f);byResolved.set(f.resolvedPath,f);
 }
 return true;
}
