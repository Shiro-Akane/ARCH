import path from 'node:path';
import {createHash} from 'node:crypto';
import {readFile,stat} from 'node:fs/promises';
import {checkedPath,selectedPath} from './files.ts';
import {profileFingerprint} from './buildProfile.ts';
import type {BuildProfile} from '../src/host/contracts.ts';
import type {ConfigureProfile} from './configureRunner.ts';

/** Read a bounded canonical project file; selected-path symlinks stay forbidden. */
async function boundedText(root:string,relative:string,limit:number){
 const target=await checkedPath(root,relative);const info=await stat(target);
 if(!info.isFile()||info.size>limit)throw new Error('Existing build evidence is missing or exceeds its read budget.');
 return readFile(target,'utf8');
}
/** Reject ambiguous cache entries rather than accepting a last-value override. */
function cacheValues(text:string){
 const values=new Map<string,string>();
 for(const line of text.split(/\r?\n/)){
  if(line.startsWith('#')||line.startsWith('//')||!line)continue;
  const match=/^([A-Za-z_][A-Za-z0-9_]*):[^=]+=(.*)$/.exec(line);if(!match)continue;
  if(values.has(match[1]))throw new Error('Duplicate CMake cache key: '+match[1]);values.set(match[1],match[2]);
 }
 return values;
}
/** Preserve the actual configured CPU/CUDA mode; unknown mode is not a default. */
function booleanCache(values:Map<string,string>,key:string,required=false){
 const value=values.get(key);
 if(value===undefined&&!required)return false;
 if(value!==undefined&&/^(ON|TRUE|1|YES)$/i.test(value))return true;
 if(value!==undefined&&/^(OFF|FALSE|0|NO)$/i.test(value))return false;
 throw new Error('Existing CMake cache has no unambiguous '+key+' setting.');
}

/** Adopt only an explicit, already-configured in-project Ninja ARCH tree.
 * The launcher chooses the directory; browser requests receive an immutable
 * Host profile ID. Configure adds no -D overrides to this existing tree.
 */
export async function existingBuildProfile(root:string,buildDirRelative:string,binaryRelative?:string){
 selectedPath(buildDirRelative);
 const build=await checkedPath(root,buildDirRelative);if(!(await stat(build)).isDirectory())throw new Error('Existing build path is not a directory.');
 const cacheRelative=buildDirRelative+'/CMakeCache.txt';
 const values=cacheValues(await boundedText(root,cacheRelative,2*1024*1024));
 if(values.get('CMAKE_HOME_DIRECTORY')!==root||values.get('CMAKE_CACHEFILE_DIR')!==build)
  throw new Error('Existing CMake source/build binding differs; refusing to rebind it.');
 if(values.get('CMAKE_GENERATOR')!=='Ninja')throw new Error('Existing build selection requires its configured Ninja generator.');
 const cuda=booleanCache(values,'ARCH_ENABLE_CUDA',true);
 const output=values.get('ARCH_RUNTIME_OUTPUT_DIRECTORY');
 if(!output||!path.isAbsolute(output))throw new Error('Existing ARCH output directory identity is unavailable.');
 const outputParent=path.relative(root,output);selectedPath(outputParent);
 if(await checkedPath(root,outputParent)!==output)throw new Error('Existing output directory differs from its canonical identity.');
 const expected=outputParent+'/ARCH';
 if(binaryRelative!==undefined&&binaryRelative!==expected)throw new Error('Selected binary differs from the configured ARCH output.');
 const targetDirs=await boundedText(root,buildDirRelative+'/CMakeFiles/TargetDirectories.txt',1024*1024);
 if(!targetDirs.split(/\r?\n/).includes(build+'/CMakeFiles/ARCH.dir'))throw new Error('Existing build has no configured ARCH target.');
 const ninja=await boundedText(root,buildDirRelative+'/build.ninja',16*1024*1024);
 if(!/^build ARCH: phony(?: |$)/m.test(ninja))throw new Error('Existing Ninja ARCH target is unavailable.');
 const emitLink=booleanCache(values,'ARCH_EMIT_LINK_DEPENDENCIES');
 const retainLink=booleanCache(values,'ARCH_RETAIN_LTO_LINK_INPUTS');
 if(retainLink&&!emitLink)throw new Error('Existing LTO retention has no linker dependency authority.');
 const id='studio-existing-'+createHash('sha256').update(buildDirRelative+'\0'+(cuda?'cuda':'cpu')).digest('hex').slice(0,16);
 const profile:BuildProfile={id,displayName:'Existing '+(cuda?'CUDA':'CPU')+' ARCH build',managedSourceRoot:root,
  buildDirRelative,target:'ARCH',outputBinaryRelative:expected,parallelism:2,
  compilerDependencyMode:'ninja',dependenciesComplete:false,buildBackend:cuda?'cuda':'cpu',
  ...(emitLink?{linkDependencyFile:'ARCH.link.d'}:{}),...(retainLink?{retainGnuLtoInputs:true}:{}),
  trackedInputs:['CMakeLists.txt','CMakePresets.json',cacheRelative]};
 const configure:ConfigureProfile={id,sourceRoot:root,buildDirRelative,generator:'Ninja',definitions:{}};
 return {build:profile,configure};
}

/** Recheck binding before an explicit Build; a changed backend/output is rejected. */
export async function validateExistingBuildProfile(root:string,profile:BuildProfile){
 const current=await existingBuildProfile(root,profile.buildDirRelative,profile.outputBinaryRelative);
 const expected={...profile};delete expected.sourceRelativePath;
 expected.trackedInputs=expected.trackedInputs.filter(p=>p!==profile.sourceRelativePath);
 if(profileFingerprint(current.build)!==profileFingerprint(expected))throw new Error('Existing build profile binding changed; reopen the selected project explicitly.');
}
