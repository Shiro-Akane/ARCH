/**
 * Describe approved build recipes and validate their project-relative paths.
 * Profile validation checks the source/build/output boundary before any command
 * is launched. This module selects build inputs; BuildRunner owns execution and
 * the Core still owns numerical configuration and backend eligibility.
 */
import path from 'node:path';
import {sameCMakePath} from './cmakeEvidence.ts';
import {windowsEnvironment} from './hostProcess.ts';
import type {WindowsHostTools} from './hostProcess.ts';
import {access,readFile,stat} from 'node:fs/promises';
import {constants} from 'node:fs';
import {createHash} from 'node:crypto';
import {checkedPath,selectedPath} from './files.ts';
import type {BuildProfile} from '../src/host/contracts.ts';
export const CMAKE='/usr/bin/cmake';
export function profileFingerprint(p:BuildProfile){return createHash('sha256').update(JSON.stringify(p)).digest('hex');}
export async function validateProfile(root:string,p:BuildProfile,cmake=CMAKE,tools?:WindowsHostTools){
 if(!['linux','win32'].includes(process.platform))throw new Error('Unsupported Studio Build platform.');
 if(p.observedInputScope!==undefined && (p.observedInputScope!=='observed-build-inputs-v1'||
    process.platform!=='win32'||p.buildBackend!=='cpu'||p.dependenciesComplete!==false||
    p.compilerDependencyMode!=='ninja'||!p.linkDependencyFile||p.retainGnuLtoInputs!==true))
  throw new Error('Observed Build input scope requires the reviewed native Windows CPU/Ninja profile.');
 if(process.platform==='win32'){
  if(!tools?.ninja||!tools.objdump||!tools.publisher||!tools.rc)throw new Error('Native Studio Build requires trusted generator, resource compiler, PE inspector and manifest publisher tools.');
  windowsEnvironment(tools);
  if(cmake!==tools.cmake)throw new Error('Build CMake differs from trusted native tools.');
  for(const executable of [tools.cmake,tools.supervisor,tools.ninja,tools.objdump,tools.publisher,tools.rc]){await access(executable,constants.X_OK);if(!(await stat(executable)).isFile())throw new Error('Native Build tool unavailable.');}
 }
 if(root!==p.managedSourceRoot)throw new Error('Profile source root does not match the managed project.');
 if(!/^[a-zA-Z0-9_-]+$/.test(p.id)||!p.displayName||!/^[-a-zA-Z0-9_.+]+$/.test(p.target)||p.target.startsWith('-'))throw new Error('Invalid fixed profile identity or target.');
 if(!Number.isInteger(p.parallelism)||p.parallelism<1||p.parallelism>28)throw new Error('Invalid fixed parallelism.');
 if(p.retainGnuLtoInputs!==undefined&&typeof p.retainGnuLtoInputs!=='boolean')throw new Error('Invalid Host-owned LTO retention mode.');
 if(p.retainGnuLtoInputs&&(p.compilerDependencyMode!=='ninja'||!p.linkDependencyFile))throw new Error('LTO retention requires compiler and linker dependency capture.');
 if(p.linkDependencyFile)selectedPath(p.linkDependencyFile);
 for(const rel of [p.buildDirRelative,p.outputBinaryRelative,...p.trackedInputs,...(p.sourceRelativePath?[p.sourceRelativePath]:[])])selectedPath(rel);
 if(p.trackedInputs.length>128||p.trackedInputs.some(x=>x.endsWith('.par')))throw new Error('Invalid tracked build inputs; runtime config is not compiled.');
 for(const rel of new Set([...p.trackedInputs,...(p.sourceRelativePath?[p.sourceRelativePath]:[])])){
  try{if(!(await stat(await checkedPath(root,rel))).isFile())throw new Error('Not a file');}
  catch{throw new Error('Tracked input unavailable; profile migration required: '+rel);}
 }
 const dir=await checkedPath(root,p.buildDirRelative);if(!(await stat(dir)).isDirectory())throw new Error('Build directory is not configured.');
 const cachePath=await checkedPath(root,p.buildDirRelative+'/CMakeCache.txt');if((await stat(cachePath)).size>2*1024*1024)throw new Error('CMake cache exceeds limit.');
 const cache=await readFile(cachePath,'utf8');
 const bound=(key:string,expected:string)=>{const prefix=key+':INTERNAL=';const values=cache.split(/\r?\n/).filter(line=>line.startsWith(prefix)).map(line=>line.slice(prefix.length));return values.length===1&&sameCMakePath(values[0],expected);};
 if(!bound('CMAKE_HOME_DIRECTORY',root))throw new Error('CMake source root differs from managed source root.');
 if(!bound('CMAKE_CACHEFILE_DIR',dir))throw new Error('CMake build directory binding differs.');
 if(p.observedInputScope){
  const cuda=cache.split(/\r?\n/).filter(line=>line.startsWith('ARCH_ENABLE_CUDA:BOOL='));
  if(cuda.length!==1||cuda[0]!=='ARCH_ENABLE_CUDA:BOOL=OFF')
   throw new Error('Observed Build input scope requires an explicitly configured CPU backend.');
 }
 const outputParent=path.dirname(p.outputBinaryRelative);await checkedPath(root,outputParent);
 await access(cmake,constants.X_OK);if(!(await stat(cmake)).isFile())throw new Error('CMake executable unavailable.');
 return dir;
}
