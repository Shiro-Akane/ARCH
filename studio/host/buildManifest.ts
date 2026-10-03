import {fingerprintLinkDependencies} from './linkDependencies.ts';
import path from 'node:path';
import {readBuildToolchainEvidence,readBuildConfigurationInputs,sameConfigurationInputs} from './cmakeEvidence.ts';
import type {CompilerInputSnapshot} from './ninjaDependencies.ts';
import {fingerprintNinjaDependencies,sameCompilerInputs} from './ninjaDependencies.ts';
import {fingerprint,checkedPath} from './files.ts';
import {profileFingerprint} from './buildProfile.ts';
import {execFile} from 'node:child_process';import {promisify} from 'node:util';
import {mkdir,open,rename,unlink} from 'node:fs/promises';import {constants} from 'node:fs';import {randomUUID,createHash} from 'node:crypto';
import type {BuildProfile,FileFingerprint,InputFingerprint,BuildManifest} from '../src/host/contracts.ts';
export async function inspect(root:string,relative:string,binary=false):Promise<FileFingerprint>{const f=await fingerprint(root,relative,binary?'executable':'case-source');if(!f.exists||f.error||!f.sha256||f.size===undefined||!f.modifiedTime)throw new Error(binary?'Expected executable is missing or cannot be fingerprinted.':`Tracked input unavailable: ${relative}`);return {sha256:f.sha256,size:f.size,modifiedTime:f.modifiedTime};}
export async function inputs(p:BuildProfile):Promise<InputFingerprint[]>{const paths=[...new Set([...(p.sourceRelativePath?[p.sourceRelativePath]:[]),...p.trackedInputs])];const list:InputFingerprint[]=[];for(const relativePath of paths)list.push({relativePath,fingerprint:await inspect(p.managedSourceRoot,relativePath)});return list;}
export function same(a:FileFingerprint,b:FileFingerprint){return a.sha256===b.sha256&&a.size===b.size&&a.modifiedTime===b.modifiedTime;}
export function sameInputs(a:InputFingerprint[],b:InputFingerprint[]){return a.length===b.length&&a.every(x=>b.some(y=>y.relativePath===x.relativePath&&same(x.fingerprint,y.fingerprint)));}
export async function gitIdentity(root:string){try{const env={PATH:'/usr/bin:/bin',HOME:process.env.HOME??'/home/arch',GIT_OPTIONAL_LOCKS:'0'};const exec=promisify(execFile);const head=await exec('/usr/bin/git',['-C',root,'rev-parse','HEAD'],{env,timeout:3000,maxBuffer:262144});const status=await exec('/usr/bin/git',['-C',root,'status','--porcelain'],{env,timeout:3000,maxBuffer:262144});return {sourceGitHead:head.stdout.trim(),repositoryDirty:!!status.stdout.trim()};}catch{return {};}}
function filename(p:BuildProfile){return 'build-'+createHash('sha256').update(p.id).digest('hex').slice(0,24)+'.json';}
async function stateDirectory(root:string,create:boolean){await checkedPath(root,'studio');if(create){await mkdir(root+'/studio/.local',{recursive:false}).catch((e:NodeJS.ErrnoException)=>{if(e.code!=='EEXIST')throw e;});}return checkedPath(root,'studio/.local');}
export async function saveManifest(p:BuildProfile,m:BuildManifest){const directory=await stateDirectory(p.managedSourceRoot,true);const dir=await open(directory,constants.O_RDONLY|constants.O_DIRECTORY|constants.O_NOFOLLOW);const temp=`/proc/self/fd/${dir.fd}/.build-${randomUUID()}.tmp`;const target=`/proc/self/fd/${dir.fd}/${filename(p)}`;let created=false;try{const f=await open(temp,constants.O_CREAT|constants.O_EXCL|constants.O_WRONLY|constants.O_NOFOLLOW,0o600);created=true;try{await f.writeFile(JSON.stringify(m,null,2));await f.sync();}finally{await f.close();}await checkedPath(p.managedSourceRoot,'studio/.local');await rename(temp,target);created=false;await dir.sync().catch(()=>undefined);}finally{if(created)await unlink(temp).catch(()=>undefined);await dir.close();}}
function validCompilerEvidence(m:BuildManifest){
 const sha=(value:unknown)=>typeof value==='string'&&/^[a-f0-9]{64}$/.test(value);
 const size=(value:unknown)=>Number.isSafeInteger(value)&&Number(value)>=0;
 if(m.configurationInputError!==undefined&&typeof m.configurationInputError!=='string')return false;
 if(m.configurationInputsStableDuringBuild!==undefined&&typeof m.configurationInputsStableDuringBuild!=='boolean')return false;
 if(m.configurationInputs){
  const c=m.configurationInputs,seen=new Set<string>();
  if(c.kind!=='cmake-configuration-inputs'||c.version!==1||c.sourceRoot!==m.managedSourceRoot||c.buildDirectory!==m.buildDirectory||!sha(c.replySha256)||!Array.isArray(c.inputs)||!c.inputs.length||c.inputs.length>20000)return false;
  if(c.generatorTools!==undefined){
   if(!Array.isArray(c.generatorTools)||c.generatorTools.length!==2)return false;
   const roles=new Set<string>();
   for(const t of c.generatorTools){
    if(!t||!['cmake','ninja'].includes(t.role)||roles.has(t.role)||!path.isAbsolute(t.path)||!path.isAbsolute(t.resolvedPath)||t.path.includes('\0')||t.resolvedPath.includes('\0')||!sha(t.sha256)||!size(t.size))return false;
    roles.add(t.role);
   }
  }
  for(const f of c.inputs){
   if(!f||typeof f.path!=='string'||!path.isAbsolute(f.path)||f.path.includes('\0')||seen.has(f.path)||!sha(f.sha256)||!size(f.size)||![f.generated,f.external,f.cmake].every(v=>typeof v==='boolean'))return false;
   seen.add(f.path);
  }
 }
 if(m.linkInputError!==undefined&&typeof m.linkInputError!=='string')return false;
 if(m.linkInputs!==undefined){
  const l=m.linkInputs,seen=new Set<string>();
  const absolute=(v:unknown)=>typeof v==='string'&&path.isAbsolute(v)&&!v.includes('\0');
  if(l.kind!=='linker-inputs'||!sha(l.depfileSha256)||!Array.isArray(l.files)||!Array.isArray(l.unavailable)||l.files.length+l.unavailable.length<1||l.files.length+l.unavailable.length>20000)return false;
  for(const f of l.files){if(!f||!absolute(f.path)||!absolute(f.resolvedPath)||!sha(f.sha256)||!size(f.size)||seen.has(f.path))return false;seen.add(f.path);}
  for(const f of l.unavailable){if(!f||!absolute(f.path)||f.reason!=='missing'||seen.has(f.path))return false;seen.add(f.path);}
 }
 if(m.compilerInputError!==undefined&&typeof m.compilerInputError!=='string')return false;
 if(m.compilerDriverError!==undefined&&typeof m.compilerDriverError!=='string')return false;
 if(m.compilerInputsStableDuringBuild!==undefined&&typeof m.compilerInputsStableDuringBuild!=='boolean')return false;
 if(m.compilerInputs!==undefined){
  const c=m.compilerInputs;
  if(c.kind!=='ninja-compiler-inputs'||!Number.isSafeInteger(c.objectCount)||c.objectCount<1||!Array.isArray(c.files)||!c.files.length||c.files.length>20000)return false;
  const seen=new Set<string>();
  for(const f of c.files){
   if(!f||typeof f.path!=='string'||!path.isAbsolute(f.path)||f.path.includes('\0')||seen.has(f.path)||!sha(f.sha256)||!size(f.size))return false;
   seen.add(f.path);
  }
 }
 if(m.compilerDriversStableDuringBuild!==undefined&&typeof m.compilerDriversStableDuringBuild!=='boolean')return false;
 for(const drivers of [m.compilerDrivers,m.preBuildCompilerDrivers])if(drivers!==undefined){
  if(!Array.isArray(drivers)||drivers.length>16)return false;
  for(const c of drivers){
   if(!c||typeof c.path!=='string'||!path.isAbsolute(c.path)||typeof c.resolvedPath!=='string'||!path.isAbsolute(c.resolvedPath)||!sha(c.sha256)||!size(c.size)||![c.language,c.id,c.version].every(v=>typeof v==='string'&&v.length>0))return false;
   if(c.specsSha256!==undefined&&!sha(c.specsSha256))return false;
   if(c.components!==undefined){
    if(!Array.isArray(c.components)||!c.components.length||c.components.length>16)return false;
    const roles=new Set<string>();
    for(const f of c.components){
     if(!f||typeof f.role!=='string'||!f.role||roles.has(f.role)||typeof f.path!=='string'||!path.isAbsolute(f.path)||f.path.includes('\0')||typeof f.resolvedPath!=='string'||!path.isAbsolute(f.resolvedPath)||f.resolvedPath.includes('\0')||!sha(f.sha256)||!size(f.size))return false;
     roles.add(f.role);
    }
   }
  }
 }
 return true;
}
export async function loadManifest(p:BuildProfile):Promise<BuildManifest|undefined>{try{await stateDirectory(p.managedSourceRoot,false);const target=await checkedPath(p.managedSourceRoot,'studio/.local/'+filename(p));const f=await open(target,constants.O_RDONLY|constants.O_NOFOLLOW|constants.O_NONBLOCK);let m:BuildManifest;try{const s=await f.stat();if(!s.isFile()||s.size>4*1024*1024)return;m=JSON.parse(await f.readFile('utf8'));}finally{await f.close();}if(m.manifestVersion!=='1'||m.managedSourceRoot!==p.managedSourceRoot||m.profileId!==p.id||typeof m.buildId!=='string'||typeof m.buildProfileFingerprint!=='string'||!Array.isArray(m.trackedInputFingerprints)||!Array.isArray(m.preBuildInputFingerprints)||m.trackedInputFingerprints.length>128||typeof m.inputsStableDuringBuild!=='boolean'||!m.outputBinary?.fingerprint?.sha256)return;for(const x of m.trackedInputFingerprints){if(typeof x.relativePath!=='string'||typeof x.fingerprint?.sha256!=='string')return;}if(!validCompilerEvidence(m))return;return m;}catch{return;}}
export function sameToolchain(a:BuildManifest['compilerDrivers'],b:BuildManifest['compilerDrivers']){
 if(!a?.length||!b?.length)return undefined;
 const normalize=(drivers:NonNullable<BuildManifest['compilerDrivers']>)=>drivers.map(d=>({
  language:d.language,path:d.path,resolvedPath:d.resolvedPath,id:d.id,version:d.version,sha256:d.sha256,size:d.size,
  specsSha256:d.specsSha256,components:d.components?.map(c=>({role:c.role,path:c.path,resolvedPath:c.resolvedPath,sha256:c.sha256,size:c.size})).sort((x,y)=>x.role.localeCompare(y.role)),
 })).sort((x,y)=>x.language.localeCompare(y.language));
 return JSON.stringify(normalize(a))===JSON.stringify(normalize(b));
}
export async function makeManifest(p:BuildProfile,projectId:string,buildId:string,startedAt:string,before:InputFingerprint[],preBinary:FileFingerprint|undefined,git:Awaited<ReturnType<typeof gitIdentity>>,compilerBefore?:CompilerInputSnapshot,toolchainBefore?:BuildManifest['compilerDrivers'],configurationBefore?:BuildManifest['configurationInputs']):Promise<BuildManifest>{const compiler:Pick<BuildManifest,'compilerInputs'|'compilerInputError'|'compilerInputsStableDuringBuild'|'compilerDrivers'|'compilerDriverError'|'preBuildCompilerDrivers'|'compilerDriversStableDuringBuild'|'linkInputs'|'linkInputError'|'configurationInputs'|'configurationInputError'|'configurationInputsStableDuringBuild'>={};if(p.compilerDependencyMode==='ninja'){
try{compiler.configurationInputs=await readBuildConfigurationInputs(p.managedSourceRoot,p.managedSourceRoot+'/'+p.buildDirRelative);}
catch(e){compiler.configurationInputError=e instanceof Error?e.message:'CMake input identity unavailable';}
compiler.configurationInputsStableDuringBuild=sameConfigurationInputs(configurationBefore,compiler.configurationInputs);
}if(p.compilerDependencyMode==='ninja'){try{compiler.compilerInputs=await fingerprintNinjaDependencies(p.managedSourceRoot+'/'+p.buildDirRelative);}catch(e){compiler.compilerInputError=e instanceof Error?e.message:'Compiler dependency capture failed';}}if(p.compilerDependencyMode==='ninja')compiler.compilerInputsStableDuringBuild=sameCompilerInputs(compilerBefore,compiler.compilerInputs);if(p.compilerDependencyMode==='ninja'){try{compiler.compilerDrivers=(await readBuildToolchainEvidence(p.managedSourceRoot+'/'+p.buildDirRelative)).compilers;}catch(e){compiler.compilerDriverError=e instanceof Error?e.message:'Compiler driver identity unavailable';}}if(p.compilerDependencyMode==='ninja'){compiler.preBuildCompilerDrivers=toolchainBefore;compiler.compilerDriversStableDuringBuild=sameToolchain(toolchainBefore,compiler.compilerDrivers);}if(p.linkDependencyFile){try{compiler.linkInputs=await fingerprintLinkDependencies(p.managedSourceRoot+'/'+p.buildDirRelative,p.linkDependencyFile,p.managedSourceRoot+'/'+p.outputBinaryRelative);}catch(e){compiler.linkInputError=e instanceof Error?e.message:'Link input identity unavailable';}}const after=await inputs(p);const binary=await inspect(p.managedSourceRoot,p.outputBinaryRelative,true);return {...compiler,manifestVersion:'1',projectId,profileId:p.id,caseId:p.caseId,managedSourceRoot:p.managedSourceRoot,buildId,startedAt,finishedAt:new Date().toISOString(),...git,buildProfileFingerprint:profileFingerprint(p),sourceFingerprint:after.find(x=>x.relativePath===p.sourceRelativePath)?.fingerprint,trackedInputFingerprints:after,preBuildInputFingerprints:before,inputsStableDuringBuild:sameInputs(before,after),buildDirectory:p.managedSourceRoot+'/'+p.buildDirRelative,target:p.target,outputBinary:{relativePath:p.outputBinaryRelative,absolutePath:p.managedSourceRoot+'/'+p.outputBinaryRelative,fingerprint:binary},preBuildBinaryFingerprint:preBinary};}
