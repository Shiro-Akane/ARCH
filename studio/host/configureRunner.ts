import path from 'node:path';
import {mkdir,readFile,readdir,stat,writeFile} from 'node:fs/promises';
import {spawn} from 'node:child_process';
import {StringDecoder} from 'node:string_decoder';
import {randomUUID} from 'node:crypto';
import {checkedPath,selectedPath} from './files.ts';
import {BuildLog} from './buildLog.ts';
import {CMAKE} from './buildProfile.ts';
import {readCMakeConfigurationEvidence} from './cmakeEvidence.ts';
import type {CMakeConfigurationEvidence} from './cmakeEvidence.ts';
/** Created by Host configuration only. Never deserialize this type from HTTP. */
export interface ConfigureProfile {
 id:string;sourceRoot:string;buildDirRelative:string;generator:'Ninja'|'Unix Makefiles';
 definitions:Readonly<Record<string,string>>;
}
export interface ConfigureResult {id:string;profileId:string;state:'succeeded'|'failed';exitCode:number|null;error?:string;evidence?:CMakeConfigurationEvidence}
async function directory(root:string,relative:string){
 selectedPath(relative);let prefix='';
 for(const part of relative.split('/')){
  prefix=prefix?prefix+'/'+part:part;
  await mkdir(path.join(root,prefix)).catch((e:NodeJS.ErrnoException)=>{if(e.code!=='EEXIST')throw e;});
  if(!(await stat(await checkedPath(root,prefix))).isDirectory())throw new Error('Configure destination is not a directory.');
 }
 return checkedPath(root,relative);
}
/** Serial fixed-argv operation. Configure success never creates a Build Manifest. */
export class ConfigureRunner {
 readonly profile:ConfigureProfile;private active=false;private log?:BuildLog;
 constructor(profile:ConfigureProfile){this.profile=structuredClone(profile);}
 isActive(){return this.active;}
 events(){return this.log?.snapshot();}
 async run(projectId:string,profileId:string):Promise<ConfigureResult>{
  if(this.active)throw new Error('Configure already active.');
  if(profileId!==this.profile.id)throw new Error('Unknown Host Configure profile.');
  this.active=true;
  const id=randomUUID(),result:ConfigureResult={id,profileId,state:'failed',exitCode:null};
  this.log=new BuildLog(projectId,id);
  try{
   const p=this.profile;
   if(process.platform!=='linux'||!['Ninja','Unix Makefiles'].includes(p.generator))throw new Error('Unsupported Configure platform or generator.');
   await checkedPath(p.sourceRoot,'CMakeLists.txt');
   const definitions=Object.entries(p.definitions).sort(([a],[b])=>a.localeCompare(b));
   if(definitions.some(([key,value])=>!/^[_A-Za-z][_A-Za-z0-9]*$/.test(key)||typeof value!=='string'||value.includes('\0')))
    throw new Error('Invalid Host Configure definition.');
   const build=await directory(p.sourceRoot,p.buildDirRelative);
   let cache:string|undefined;
   try{cache=await readFile(await checkedPath(p.sourceRoot,p.buildDirRelative+'/CMakeCache.txt'),'utf8');}
   catch(e){if((e as NodeJS.ErrnoException).code!=='ENOENT')throw e;}
   if(cache!==undefined){
    const lines=cache.split(/\r?\n/);
    if(!lines.includes('CMAKE_HOME_DIRECTORY:INTERNAL='+p.sourceRoot)||!lines.includes('CMAKE_CACHEFILE_DIR:INTERNAL='+build)||
       !lines.includes('CMAKE_GENERATOR:INTERNAL='+p.generator))throw new Error('Existing CMake tree binding or generator differs; refusing to migrate it.');
   }else if((await readdir(build)).length)throw new Error('Unconfigured build directory is not empty.');
   const query=await directory(p.sourceRoot,p.buildDirRelative+'/.cmake/api/v1/query');
   for(const name of ['cmakeFiles-v1','codemodel-v2','cache-v2','toolchains-v1']){
    // Existing query is preserved; a symlink is never followed or overwritten.
    try{await writeFile(path.join(query,name),'',{flag:'wx'});}
    catch(e){if((e as NodeJS.ErrnoException).code!=='EEXIST')throw e;await checkedPath(p.sourceRoot,p.buildDirRelative+'/.cmake/api/v1/query/'+name);}
   }
   const args=['-S',p.sourceRoot,'-B',build,'-G',p.generator,...definitions.map(([k,v])=>'-D'+k+'='+v)];
   await new Promise<void>((resolve,reject)=>{
    const child=spawn(CMAKE,args,{cwd:p.sourceRoot,shell:false,env:{PATH:'/usr/local/bin:/usr/bin:/bin',HOME:process.env.HOME,LANG:'C.UTF-8'},stdio:['ignore','pipe','pipe']});
    for(const [stream,kind] of [[child.stdout,'stdout'],[child.stderr,'stderr']] as const){
     const decoder=new StringDecoder('utf8');stream.on('data',(b:Buffer)=>this.log?.append(kind,decoder.write(b)));
     stream.on('end',()=>{const tail=decoder.end();if(tail)this.log?.append(kind,tail);});
    }
    child.once('error',reject);child.once('close',(code)=>{result.exitCode=code;if(code===0)resolve();else reject(new Error('CMake Configure failed.'));});
   });
   const reply=await checkedPath(p.sourceRoot,p.buildDirRelative+'/.cmake/api/v1/reply');
   const indexes=(await readdir(reply)).filter(n=>/^index-.*\.json$/.test(n)).sort();
   if(!indexes.length)throw new Error('CMake did not publish a File API index.');
   const index:unknown=JSON.parse(await readFile(path.join(reply,indexes.at(-1)!),'utf8'));
   const objects=(index as {objects?:{kind:string;jsonFile:string}[]})?.objects;
   const entry=Array.isArray(objects)?objects.find(o=>o.kind==='cmakeFiles'):undefined;
   if(!entry||typeof entry.jsonFile!=='string'||path.basename(entry.jsonFile)!==entry.jsonFile)throw new Error('Missing CMake configuration input evidence.');
   result.evidence=await readCMakeConfigurationEvidence(p.sourceRoot,build,path.join(reply,entry.jsonFile));
   result.state='succeeded';
  }catch(e){result.error=e instanceof Error?e.message:'Configure failed';this.log.append('stderr',result.error);}
  finally{this.log.append('state',undefined,result.state);this.active=false;}
  return result;
 }
}
