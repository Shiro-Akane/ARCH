/**
 * Read actual CMake File API/cache evidence for a configured build.
 * Trace source inputs, generator and compiler identities through bounded stable
 * reads; ambiguous or incomplete evidence is an error or unknown freshness, not
 * a reason to infer the toolchain from a profile label.
 */
import path from 'node:path';
import {windowsEnvironment} from './hostProcess.ts';
import type {WindowsHostTools} from './hostProcess.ts';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {readFile,stat,realpath,readdir,access} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {constants} from 'node:fs';
/** CMake-owned configuration inputs; not a transitive compiler dependency claim. */
export interface CMakeInputEvidence {path:string;sha256:string;size:number;generated:boolean;external:boolean;cmake:boolean}
export interface GeneratorToolEvidence {role:'cmake'|'ninja';path:string;resolvedPath:string;sha256:string;size:number}
export interface CMakeConfigurationEvidence {
 kind:'cmake-configuration-inputs';version:1;sourceRoot:string;buildDirectory:string;
 replySha256:string;inputs:CMakeInputEvidence[];generatorTools?:GeneratorToolEvidence[];dependenciesComplete:false;
 missingCoverage:readonly ['compiler-includes','link-inputs','toolchain-identity'];
}
function object(x:unknown):x is Record<string,unknown>{return !!x&&typeof x==='object'&&!Array.isArray(x);}
function absolute(x:unknown):x is string{return typeof x==='string'&&path.isAbsolute(x)&&!x.includes('\0');}
// CMake writes '/' and may lowercase the drive in its Windows cache. Normalize
// that host spelling only; directory/file case and all Linux spelling stay exact.
export function sameCMakePath(value:unknown,expected:string){
 if(!absolute(value)||!absolute(expected))return false;
 if(process.platform!=='win32')return value===expected;
 const normalize=(text:string)=>path.normalize(text).replace(/^[a-z]:/i,drive=>drive.toUpperCase());
 return normalize(value)===normalize(expected);
}
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
    !object(data.paths)||!sameCMakePath(data.paths.source,source)||!sameCMakePath(data.paths.build,build)||
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

export const HOST_BUILD_PATH='/usr/local/cuda-12.8/bin:/usr/local/bin:/usr/bin:/bin';
type CompilerProbe=(compiler:string,argument:string)=>Promise<string>;
const compilerProbe:CompilerProbe=async(compiler,argument)=>compilerProbeWithTools(compiler,argument);
async function compilerProbeWithTools(compiler:string,argument:string,tools?:WindowsHostTools){
 if(process.platform==='win32'&&!tools)throw new Error('Trusted native compiler environment required.');
 const result=await promisify(execFile)(compiler,[argument],{
  env:tools?{...windowsEnvironment(tools),LC_ALL:'C'}:{PATH:HOST_BUILD_PATH,LC_ALL:'C'},timeout:5000,maxBuffer:1024*1024,windowsHide:true,
 });
 return argument==='/?'?result.stdout+result.stderr:result.stdout;
};
/** Only fixed, bound RC probes reach this parser; no cache-selected command. */
export function resourceCompilerVersion(output:string,msvc:boolean){
 const match=(msvc?/^Microsoft \(R\) Windows \(R\) Resource Compiler Version ([0-9]+(?:\.[0-9]+)+)\r*$/m:
  /^GNU windres \(GNU Binutils\) ([0-9]+(?:\.[0-9]+)+)\r?$/m).exec(output);
 if(!match)throw new Error((msvc?'Microsoft':'GNU')+' resource compiler version unavailable.');
 return {id:msvc?'Microsoft RC':'GNU windres',version:match[1]};
}
async function gnuComponents(compiler:string,language:string,probe:CompilerProbe,selectedProgram?:string,tools?:WindowsHostTools){
 const plugin=process.platform==='win32'?'liblto_plugin.dll':'liblto_plugin.so';
 const components:{role:string;path:string;resolvedPath:string;sha256:string;size:number}[]=[];
 const query=async(argument:string)=>{
  const value=(await probe(compiler,argument)).trim();
  if(!value||value.includes('\0')||value.includes('\n'))throw new Error('Invalid GNU component reply.');
  return value;
 };
 for(const role of [language==='CXX'?'cc1plus':'cc1','collect2','as','ld','lto1',plugin,...(selectedProgram?['selected-'+selectedProgram]:[])]){
  const program=role.startsWith('selected-')?role.slice('selected-'.length):role;
  const value=await query((role===plugin?'-print-file-name=':'-print-prog-name=')+program);
  // Bare tool names are resolved only through the same fixed Host PATH used for the probe.
  let filename=value;
  if(!path.isAbsolute(value)){
   if(path.basename(value)!==value||role===plugin)throw new Error('GNU component cannot be resolved: '+program);
   filename='';
   for(const dir of (tools?.path??HOST_BUILD_PATH).split(path.delimiter)){
    const candidate=path.join(dir,process.platform==='win32'&&!value.toLowerCase().endsWith('.exe')?value+'.exe':value);
    try{await stat(candidate);filename=candidate;break;}catch(e){if((e as NodeJS.ErrnoException).code!=='ENOENT')throw e;}
   }
   if(!filename)throw new Error('GNU component unavailable: '+program);
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
export async function readCMakeToolchainEvidence(buildDirectory:string,replyFile:string,probe:CompilerProbe=compilerProbe,selectedProgram?:string,tools?:WindowsHostTools){
 if(tools&&probe===compilerProbe)probe=(compiler,argument)=>compilerProbeWithTools(compiler,argument,tools);
 const build=await realpath(buildDirectory),reply=await realpath(replyFile);
 if(path.dirname(reply)!==path.join(build,'.cmake/api/v1/reply'))throw new Error('Toolchain reply outside selected build.');
 const bytes=await boundedFile(reply,8*1024*1024),data:unknown=JSON.parse(bytes.toString('utf8'));
 if(!object(data)||data.kind!=='toolchains'||!object(data.version)||data.version.major!==1||!Array.isArray(data.toolchains)||!data.toolchains.length||data.toolchains.length>16)
  throw new Error('Incompatible CMake toolchain reply.');
 const compilers=[];
 for(const entry of data.toolchains){
  if(!object(entry)||typeof entry.language!=='string'||!object(entry.compiler)||!absolute(entry.compiler.path))throw new Error('Malformed compiler identity.');
  let id=entry.compiler.id,version=entry.compiler.version;
  // CMake's native RC descriptor omits id/version. Probe only the explicitly
 // bound resource compiler; never guess missing C/CXX identities or run a
  // cache-selected executable. Keep the actual RC driver hash like other tools.
  if(process.platform==='win32'&&entry.language==='RC'&&id===undefined&&version===undefined){
   if(!tools?.rc||!sameCMakePath(entry.compiler.path,tools.rc))throw new Error('RC driver differs from trusted Host resource compiler.');
   const msvc=data.toolchains.some(item=>object(item)&&object(item.compiler)&&item.compiler.id==='MSVC');
   const output=await probe(tools.rc,msvc?'/?':'--version');
   ({id,version}=resourceCompilerVersion(output,msvc));
  }
  if(typeof id!=='string'||!id||typeof version!=='string'||!version)throw new Error('Malformed compiler identity.');
  const resolvedPath=await realpath(entry.compiler.path),content=await boundedFile(resolvedPath,128*1024*1024);
  compilers.push({language:entry.language,path:entry.compiler.path,resolvedPath,id,version,
   sha256:createHash('sha256').update(content).digest('hex'),size:content.length,
   ...(entry.compiler.id==='GNU'&&['C','CXX'].includes(entry.language)?await gnuComponents(entry.compiler.path,entry.language,probe,entry.language==='CXX'?selectedProgram:undefined,tools):{})});
 }
 return {kind:'cmake-compiler-driver-identities' as const,replySha256:createHash('sha256').update(bytes).digest('hex'),compilers,
  dependenciesComplete:false as const,missingCoverage:['compiler-subprograms','linker','implicit-libraries']};
}

async function buildReply(buildDirectory:string,kind:string){
 const build=await realpath(buildDirectory),reply=path.join(build,'.cmake/api/v1/reply');
 const names=(await readdir(reply)).filter(n=>/^index-.*\.json$/.test(n)).sort();
 if(!names.length)throw new Error('No CMake File API index.');
 const index:unknown=JSON.parse((await boundedFile(path.join(reply,names.at(-1)!),8*1024*1024)).toString('utf8'));
 if(!object(index)||!Array.isArray(index.objects))throw new Error('Malformed CMake File API index.');
 const entry=index.objects.find(o=>object(o)&&o.kind===kind);
 if(!object(entry)||typeof entry.jsonFile!=='string'||path.basename(entry.jsonFile)!==entry.jsonFile)
  throw new Error('Missing '+kind+' reply reference.');
 return {build,replyFile:path.join(reply,entry.jsonFile)};
}

export async function readBuildToolchainEvidence(buildDirectory:string,tools?:WindowsHostTools){
 const {build,replyFile}=await buildReply(buildDirectory,'toolchains');
 // Read only the bounded descriptor to decide whether GNU CXX needs the
 // actual ARCH linker selection. Do not probe or hash the full toolchain yet.
 const bytes=await boundedFile(replyFile,8*1024*1024),data:unknown=JSON.parse(bytes.toString('utf8'));
 if(!object(data)||data.kind!=='toolchains'||!object(data.version)||data.version.major!==1||
    !Array.isArray(data.toolchains)||!data.toolchains.length||data.toolchains.length>16)
  throw new Error('Incompatible CMake toolchain reply.');
 const hasGnuCxx=data.toolchains.some(entry=>object(entry)&&entry.language==='CXX'&&
  object(entry.compiler)&&entry.compiler.id==='GNU');
 // Non-GNU fixtures retain their old contract: a codemodel is not required.
 const selectedProgram=hasGnuCxx?(await readBuildLinkerSelection(build)).programName:undefined;
 const evidence=await readCMakeToolchainEvidence(build,replyFile,(compiler,argument)=>compilerProbeWithTools(compiler,argument,tools),selectedProgram,tools);
 // A descriptor replacement cannot change the GNU/selection decision midway.
 if(evidence.replySha256!==createHash('sha256').update(bytes).digest('hex'))
  throw new Error('Toolchain reply changed while selecting linker.');
 return evidence;
}
/** Fingerprint fixed Host tools; cache program paths are never executed. */
export async function readBuildGeneratorEvidence(sourceRoot:string,buildDirectory:string,hostCmake='/usr/bin/cmake',hostNinja?:string):Promise<GeneratorToolEvidence[]>{
 const source=await realpath(sourceRoot),build=await realpath(buildDirectory);
 const cache=path.join(build,'CMakeCache.txt'),bytes=await boundedFile(cache,2*1024*1024);
 const values=new Map<string,string>();
 for(const line of bytes.toString('utf8').split(/\r?\n/)){
  if(!line||line.startsWith('#')||line.startsWith('//')||!line.includes('='))continue;
  const at=line.indexOf('='),key=line.slice(0,at).split(':')[0];
  if(values.has(key))throw new Error('Duplicate CMake cache key: '+key);
  values.set(key,line.slice(at+1));
 }
 if(!sameCMakePath(values.get('CMAKE_HOME_DIRECTORY'),source)||!sameCMakePath(values.get('CMAKE_CACHEFILE_DIR'),build))
  throw new Error('Generator source/build binding differs.');
 if(values.get('CMAKE_GENERATOR')!=='Ninja')throw new Error('Unsupported Host generator.');
 if(!sameCMakePath(values.get('CMAKE_COMMAND'),hostCmake))throw new Error('Cache CMake differs from fixed Host CMake.');
 // Original read-only callers may only fingerprint the cache tool. The Build
 // path always supplies its explicit Ninja binding before any execution.
 if(hostNinja!==undefined&&!sameCMakePath(values.get('CMAKE_MAKE_PROGRAM'),hostNinja))throw new Error('Cache Ninja differs from fixed Host Ninja.');
 const tools:GeneratorToolEvidence[]=[];
 for(const role of ['cmake','ninja'] as const){
  const filename=role==='cmake'?hostCmake:values.get('CMAKE_MAKE_PROGRAM');
  if(!absolute(filename))throw new Error('Absolute generator tool path required.');
  const resolvedPath=await realpath(filename);await access(resolvedPath,constants.X_OK);
  const content=await boundedFile(resolvedPath,128*1024*1024);
  if(await realpath(filename)!==resolvedPath)throw new Error('Generator tool target changed while reading.');
  tools.push({role,path:filename,resolvedPath,sha256:createHash('sha256').update(content).digest('hex'),size:content.length});
 }
 if(!(await boundedFile(cache,2*1024*1024)).equals(bytes))throw new Error('Generator cache changed while reading.');
 return tools;
}
export async function readBuildConfigurationInputs(sourceRoot:string,buildDirectory:string,tools?:WindowsHostTools){
 if(process.platform==='win32'&&!tools?.ninja)throw new Error('Trusted native generator tools required.');
 const {build,replyFile}=await buildReply(buildDirectory,'cmakeFiles');
 const evidence=await readCMakeConfigurationEvidence(sourceRoot,build,replyFile);
 evidence.generatorTools=await readBuildGeneratorEvidence(sourceRoot,build,tools?.cmake,tools?.ninja);
 return evidence;
}
export function sameConfigurationInputs(a:CMakeConfigurationEvidence|undefined,b:CMakeConfigurationEvidence|undefined){
 if(!a||!b||!a.generatorTools||!b.generatorTools||a.generatorTools.length!==2||b.generatorTools.length!==2||a.sourceRoot!==b.sourceRoot||a.buildDirectory!==b.buildDirectory||a.inputs.length!==b.inputs.length)return false;
 const tools=new Map(a.generatorTools.map(t=>[t.role,t]));
 if(!b.generatorTools.every(t=>{const old=tools.get(t.role);return old?.path===t.path&&old.resolvedPath===t.resolvedPath&&old.sha256===t.sha256&&old.size===t.size;}))return false;
 const prior=new Map(a.inputs.map(f=>[f.path,f]));
 return b.inputs.every(f=>{
  const old=prior.get(f.path);
  return old?.sha256===f.sha256&&old.size===f.size&&old.generated===f.generated&&old.external===f.external&&old.cmake===f.cmake;
 });
}

/** Read selection only; never execute CMake command fragments or claim observed linker execution. */
export async function readBuildLinkerSelection(buildDirectory:string){
 const {build,replyFile}=await buildReply(buildDirectory,'codemodel');
 const bytes=await boundedFile(replyFile,8*1024*1024),data:unknown=JSON.parse(bytes.toString('utf8'));
 if(!object(data)||data.kind!=='codemodel'||!object(data.version)||data.version.major!==2||
    !object(data.paths)||!sameCMakePath(data.paths.build,build)||!Array.isArray(data.configurations)||data.configurations.length!==1)
  throw new Error('Unsupported ARCH codemodel binding or configuration count.');
 const configuration=data.configurations[0];
 if(!object(configuration)||!Array.isArray(configuration.targets))throw new Error('Malformed codemodel targets.');
 const targets=configuration.targets.filter(t=>object(t)&&t.name==='ARCH');
 if(targets.length!==1||!object(targets[0])||typeof targets[0].jsonFile!=='string'||
    path.basename(targets[0].jsonFile)!==targets[0].jsonFile)throw new Error('Missing or ambiguous ARCH target.');
 const targetFile=path.join(path.dirname(replyFile),targets[0].jsonFile);
 if(path.dirname(await realpath(targetFile))!==path.dirname(replyFile))throw new Error('Target reply escapes build tree.');
 const targetBytes=await boundedFile(targetFile,8*1024*1024),target:unknown=JSON.parse(targetBytes.toString('utf8'));
 if(!object(target)||target.name!=='ARCH'||target.type!=='EXECUTABLE'||!object(target.link)||
    target.link.language!=='CXX'||!Array.isArray(target.link.commandFragments))
  throw new Error('Unsupported ARCH link description.');
 const flags:string[]=[];
 for(const entry of target.link.commandFragments){
  if(!object(entry)||typeof entry.fragment!=='string'||typeof entry.role!=='string'||entry.fragment.includes('\0'))
   throw new Error('Malformed link command fragment.');
  if(entry.role==='flags')flags.push(entry.fragment);
 }
 const tokens=flags.join(' ').trim().split(/\s+/).filter(Boolean);
 // Overrides require their own driver-resolution contract; guessing through them is unsafe.
 if(tokens.some(t=>t.startsWith('-B')||t.startsWith('--sysroot')||t.startsWith('-specs')||
    t.startsWith('@')||/["'\\]/.test(t)))throw new Error('Unsupported linker resolution override.');
 const selections=tokens.filter(t=>t.startsWith('-fuse-ld'));
 if(selections.length>1)throw new Error('Ambiguous linker selection.');
 const option=selections[0]??null;
 if(option!==null&&!['-fuse-ld=mold','-fuse-ld=lld','-fuse-ld=bfd','-fuse-ld=gold'].includes(option))
  throw new Error('Unsupported linker selection.');
 return {kind:'cmake-linker-selection' as const,target:'ARCH' as const,language:'CXX' as const,
  option,programName:option===null?'ld':'ld.'+option.slice('-fuse-ld='.length),
  codemodelSha256:createHash('sha256').update(bytes).digest('hex'),
  targetReplySha256:createHash('sha256').update(targetBytes).digest('hex'),
  observedExecution:false as const};
}
