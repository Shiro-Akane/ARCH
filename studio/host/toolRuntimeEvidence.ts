import path from 'node:path';
import {open,realpath} from 'node:fs/promises';
import {constants} from 'node:fs';
import {createHash} from 'node:crypto';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import type {CMakeConfigurationEvidence} from './cmakeEvidence.ts';
import type {BuildManifest} from '../src/host/contracts.ts';

export interface RuntimeFile {path:string;resolvedPath:string;sha256:string;size:number}
export interface ToolRuntimeEvidence {
 kind:'static-tool-runtime';version:1;dependenciesComplete:false;
 roots:string[];inspectors:RuntimeFile[];loaderCache:RuntimeFile;
 nodes:(RuntimeFile&{needed:string[];interpreter:string|null;searchPaths:string[]})[];
 edges:{parent:string;requested:string;candidate:string;kind:'interpreter'|'DT_NEEDED'}[];
 unresolved:{parent:string;requested:string;reason:'unmodeled-rpath'|'missing-or-ambiguous-cache-candidate';candidates:string[]}[];
}
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
export async function readToolRuntimeEvidence(roots:string[],probe:Capture=capture):Promise<ToolRuntimeEvidence>{
 roots=[...new Set(roots)].sort();if(!roots.length||roots.length>32||roots.some(s=>!absolute(s)))throw new Error('Runtime root budget or path invalid.');
 const inspectors=await Promise.all([READELF,LDCONFIG].map(runtimeFingerprint)),loaderCache=await runtimeFingerprint('/etc/ld.so.cache');
 const cache=parseRuntimeCache(await probe(LDCONFIG,['-p']));
 const nodes:ToolRuntimeEvidence['nodes']=[],edges:ToolRuntimeEvidence['edges']=[],unresolved:ToolRuntimeEvidence['unresolved']=[];
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
export function toolRuntimeRoots(configuration:CMakeConfigurationEvidence|undefined,drivers:BuildManifest['compilerDrivers']){
 if(!configuration?.generatorTools?.length||!drivers?.length)throw new Error('Runtime root identities unavailable.');
 return [...configuration.generatorTools.map(t=>t.path),...drivers.flatMap(d=>[d.path,...(d.components??[]).filter(c=>c.role!=='specs').map(c=>c.path)])];
}
function sameRuntimeFile(a:RuntimeFile,b:RuntimeFile){return a.path===b.path&&a.resolvedPath===b.resolvedPath&&a.sha256===b.sha256&&a.size===b.size;}
export function changedRuntimeInputs(a:ToolRuntimeEvidence,b:ToolRuntimeEvidence){
 const prior=new Map([...a.nodes,...a.inspectors,a.loaderCache].map(f=>[f.path,f]));
 const changed:string[]=[];
 for(const f of [...b.nodes,...b.inspectors,b.loaderCache]){const old=prior.get(f.path);if(!old||!sameRuntimeFile(old,f))changed.push(f.path);prior.delete(f.path);}
 changed.push(...prior.keys());
 if(JSON.stringify(a.roots)!==JSON.stringify(b.roots)||JSON.stringify(a.edges)!==JSON.stringify(b.edges)||JSON.stringify(a.unresolved)!==JSON.stringify(b.unresolved))changed.push('static tool runtime dependency graph');
 return [...new Set(changed)];
}
export function sameRuntimeInputs(a:ToolRuntimeEvidence|undefined,b:ToolRuntimeEvidence|undefined){return !!a&&!!b&&changedRuntimeInputs(a,b).length===0;}
export function validRuntimeEvidence(value:unknown):value is ToolRuntimeEvidence{
 if(!value||typeof value!=='object')return false;
 const c=value as ToolRuntimeEvidence;
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
