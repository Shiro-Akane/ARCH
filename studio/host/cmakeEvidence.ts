import path from 'node:path';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {readFile,stat,realpath,readdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
/** CMake-owned configuration inputs; not a transitive compiler dependency claim. */
export interface CMakeInputEvidence {path:string;sha256:string;size:number;generated:boolean;external:boolean;cmake:boolean}
export interface CMakeConfigurationEvidence {
 kind:'cmake-configuration-inputs';version:1;sourceRoot:string;buildDirectory:string;
 replySha256:string;inputs:CMakeInputEvidence[];dependenciesComplete:false;
 missingCoverage:readonly ['compiler-includes','link-inputs','toolchain-identity'];
}
function object(x:unknown):x is Record<string,unknown>{return !!x&&typeof x==='object'&&!Array.isArray(x);}
function absolute(x:unknown):x is string{return typeof x==='string'&&path.isAbsolute(x)&&!x.includes('\0');}
async function boundedFile(file:string,limit:number){
 const before=await stat(file);
 if(!before.isFile()||before.size>limit)throw new Error('CMake evidence file unavailable or over budget: '+file);
 const bytes=await readFile(file),after=await stat(file);
 if(bytes.length>limit||before.size!==after.size||before.mtimeMs!==after.mtimeMs||before.ino!==after.ino)
  throw new Error('CMake evidence changed while reading: '+file);
 return bytes;
}
/** Caller supplies a Host-owned reply path, never a browser-provided filename. */
export async function readCMakeConfigurationEvidence(sourceRoot:string,buildDirectory:string,replyFile:string):Promise<CMakeConfigurationEvidence>{
 if(!absolute(sourceRoot)||!absolute(buildDirectory)||!absolute(replyFile))throw new Error('Absolute Host-owned paths required.');
 const source=await realpath(sourceRoot),build=await realpath(buildDirectory),reply=await realpath(replyFile);
 const replyRoot=path.join(build,'.cmake/api/v1/reply');
 if(path.dirname(reply)!==replyRoot)throw new Error('Reply is outside the selected CMake build tree.');
 const bytes=await boundedFile(reply,8*1024*1024);
 const data:unknown=JSON.parse(bytes.toString('utf8'));
 if(!object(data)||data.kind!=='cmakeFiles'||!object(data.version)||data.version.major!==1||
    !object(data.paths)||data.paths.source!==source||data.paths.build!==build||
    !Array.isArray(data.inputs)||data.inputs.length===0||data.inputs.length>20000)
  throw new Error('Incompatible CMake input reply or source/build binding.');
 const inputs:CMakeInputEvidence[]=[];const seen=new Set<string>();let total=0;
 for(const entry of data.inputs){
  if(!object(entry)||typeof entry.path!=='string'||!entry.path||entry.path.includes('\0')||
     ['isGenerated','isExternal','isCMake'].some(k=>entry[k]!==undefined&&typeof entry[k]!=='boolean'))
   throw new Error('Malformed CMake input entry.');
  const filename=path.resolve(source,entry.path);
  if(!path.isAbsolute(entry.path)&&path.relative(source,filename).startsWith('..'+path.sep))
   throw new Error('Relative CMake input escapes source root.');
  if(seen.has(filename))continue;seen.add(filename);
  const content=await boundedFile(filename,16*1024*1024);total+=content.length;
  if(total>64*1024*1024)throw new Error('CMake input evidence exceeds total byte budget.');
  inputs.push({path:filename,sha256:createHash('sha256').update(content).digest('hex'),size:content.length,
   generated:entry.isGenerated===true,external:entry.isExternal===true,cmake:entry.isCMake===true});
 }
 inputs.sort((a,b)=>a.path.localeCompare(b.path));
 return {kind:'cmake-configuration-inputs',version:1,sourceRoot:source,buildDirectory:build,
  replySha256:createHash('sha256').update(bytes).digest('hex'),inputs,dependenciesComplete:false,
  missingCoverage:['compiler-includes','link-inputs','toolchain-identity']};
}

type CompilerProbe=(compiler:string,argument:string)=>Promise<string>;
const compilerProbe:CompilerProbe=async(compiler,argument)=>{
 const result=await promisify(execFile)(compiler,[argument],{
  env:{PATH:'/usr/bin:/bin',LC_ALL:'C'},timeout:5000,maxBuffer:1024*1024,
 });
 return result.stdout;
};
async function gnuComponents(compiler:string,language:string,probe:CompilerProbe){
 const components:{role:string;path:string;resolvedPath:string;sha256:string;size:number}[]=[];
 const query=async(argument:string)=>{
  const value=(await probe(compiler,argument)).trim();
  if(!value||value.includes('\0')||value.includes('\n'))throw new Error('Invalid GNU component reply.');
  return value;
 };
 for(const role of [language==='CXX'?'cc1plus':'cc1','collect2','as','ld','lto1','liblto_plugin.so']){
  const value=await query((role==='liblto_plugin.so'?'-print-file-name=':'-print-prog-name=')+role);
  // Bare tool names are resolved only through the same fixed Host PATH used for the probe.
  let filename=value;
  if(!path.isAbsolute(value)){
   if(path.basename(value)!==value||role==='liblto_plugin.so')throw new Error('GNU component cannot be resolved: '+role);
   filename='';
   for(const dir of ['/usr/bin','/bin']){
    const candidate=path.join(dir,value);
    try{await stat(candidate);filename=candidate;break;}catch(e){if((e as NodeJS.ErrnoException).code!=='ENOENT')throw e;}
   }
   if(!filename)throw new Error('GNU component unavailable: '+role);
  }
  const resolvedPath=await realpath(filename),content=await boundedFile(resolvedPath,128*1024*1024);
  if(await realpath(filename)!==resolvedPath)throw new Error('GNU component target changed while reading.');
  components.push({role,path:filename,resolvedPath,sha256:createHash('sha256').update(content).digest('hex'),size:content.length});
 }
 const specsPath=await query('-print-file-name=specs');
 if(path.isAbsolute(specsPath)){
  const resolvedPath=await realpath(specsPath),content=await boundedFile(resolvedPath,1024*1024);
  components.push({role:'specs',path:specsPath,resolvedPath,sha256:createHash('sha256').update(content).digest('hex'),size:content.length});
 }else if(specsPath!=='specs')throw new Error('Invalid GNU specs path.');
 const specs=await probe(compiler,'-dumpspecs');
 if(!specs.trim()||Buffer.byteLength(specs)>1024*1024)throw new Error('GNU specs unavailable or oversized.');
 return {components,specsSha256:createHash('sha256').update(specs).digest('hex')};
}
/** Host-owned GNU subprocess identities; implicit libraries and per-invocation overrides remain separate evidence. */
export async function readCMakeToolchainEvidence(buildDirectory:string,replyFile:string,probe:CompilerProbe=compilerProbe){
 const build=await realpath(buildDirectory),reply=await realpath(replyFile);
 if(path.dirname(reply)!==path.join(build,'.cmake/api/v1/reply'))throw new Error('Toolchain reply outside selected build.');
 const bytes=await boundedFile(reply,8*1024*1024),data:unknown=JSON.parse(bytes.toString('utf8'));
 if(!object(data)||data.kind!=='toolchains'||!object(data.version)||data.version.major!==1||!Array.isArray(data.toolchains)||!data.toolchains.length||data.toolchains.length>16)
  throw new Error('Incompatible CMake toolchain reply.');
 const compilers=[];
 for(const entry of data.toolchains){
  if(!object(entry)||typeof entry.language!=='string'||!object(entry.compiler)||!absolute(entry.compiler.path)||
     typeof entry.compiler.id!=='string'||typeof entry.compiler.version!=='string')throw new Error('Malformed compiler identity.');
  const resolvedPath=await realpath(entry.compiler.path),content=await boundedFile(resolvedPath,128*1024*1024);
  compilers.push({language:entry.language,path:entry.compiler.path,resolvedPath,id:entry.compiler.id,version:entry.compiler.version,
   sha256:createHash('sha256').update(content).digest('hex'),size:content.length,
   ...(entry.compiler.id==='GNU'&&['C','CXX'].includes(entry.language)?await gnuComponents(entry.compiler.path,entry.language,probe):{})});
 }
 return {kind:'cmake-compiler-driver-identities' as const,replySha256:createHash('sha256').update(bytes).digest('hex'),compilers,
  dependenciesComplete:false as const,missingCoverage:['compiler-subprograms','linker','implicit-libraries']};
}

export async function readBuildToolchainEvidence(buildDirectory:string){
 const build=await realpath(buildDirectory),reply=path.join(build,'.cmake/api/v1/reply');
 const names=(await readdir(reply)).filter(n=>/^index-.*\.json$/.test(n)).sort();
 if(!names.length)throw new Error('No CMake File API index.');
 const index:unknown=JSON.parse((await boundedFile(path.join(reply,names.at(-1)!),8*1024*1024)).toString('utf8'));
 if(!object(index)||!Array.isArray(index.objects))throw new Error('Malformed CMake File API index.');
 const entry=index.objects.find(o=>object(o)&&o.kind==='toolchains');
 if(!object(entry)||typeof entry.jsonFile!=='string'||path.basename(entry.jsonFile)!==entry.jsonFile)
  throw new Error('Missing toolchains reply reference.');
 return readCMakeToolchainEvidence(build,path.join(reply,entry.jsonFile));
}
