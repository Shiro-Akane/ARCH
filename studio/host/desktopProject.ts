import path from 'node:path';
import {realpath,stat} from 'node:fs/promises';
import {BUILD_PROFILES} from './buildProfile.ts';
import {desktopRegistry,registeredSourceCase} from './desktopSource.ts';
import {localCpuProfile} from './localBuildProfile.ts';
import {checkedPath,fingerprint} from './files.ts';
import {existingBuildProfile} from './existingBuildProfile.ts';
import {openProject} from './project.ts';

/** Semantic launcher inputs; no shell, argument vector or browser build settings. */
export interface DesktopProjectLaunch {project?:string;cwd:string;source?:string;config?:string;binary?:string;caseId?:string;buildDir?:string}

/** Find one existing ARCH source root without creating or rebinding a checkout. */
async function discoverRoot(start:string){
 let current=await realpath(start);if(!(await stat(current)).isDirectory())current=path.dirname(current);
 for(;;){
  if(await stat(path.join(current,'CMakeLists.txt')).then(s=>s.isFile(),()=>false)
   &&await stat(path.join(current,'simulation')).then(s=>s.isDirectory(),()=>false))return current;
  const parent=path.dirname(current);if(parent===current)throw new Error('ARCH project not found. Choose a project containing CMakeLists.txt and simulation/.');current=parent;
 }
}

/** Open the workbench before Configure/Build, without claiming compiled readiness.
 * An absent executable keeps every Core operation behind its existing validation.
 * Existing executable registration remains authoritative and is never fabricated.
 */
export async function openDesktopProject(launch:DesktopProjectLaunch){
 const root=await discoverRoot(launch.project??launch.source??launch.cwd);
 function relative(input:string){const rel=path.relative(root,path.resolve(launch.cwd,input));if(rel.startsWith('../')||path.isAbsolute(rel))throw new Error('Selected file is outside the managed project.');return rel;}
 const binary=launch.binary?relative(launch.binary):undefined;
 const registered=BUILD_PROFILES.find(p=>p.managedSourceRoot===root&&(!binary||p.outputBinaryRelative===binary));
 const local=localCpuProfile(root).build;
 const existing=launch.buildDir?await existingBuildProfile(root,relative(launch.buildDir),binary):undefined;
 const profile=existing?.build??registered??(!binary||binary===local.outputBinaryRelative?local:undefined);
 if(!profile)throw new Error('No approved Host-owned Build Profile matches this project/binary. Register a trusted profile; build trees are never rebound automatically.');
 const config=launch.config?relative(launch.config):'simulation/Sod/Sod.par';
 const selectedSource=launch.source?relative(launch.source):undefined;
 if(selectedSource){
  if(!selectedSource.startsWith('simulation/')||!selectedSource.endsWith('.cpp'))throw new Error('New user source must be a .cpp file under the project simulation/ directory.');
  const selected=await checkedPath(root,selectedSource);if(!(await stat(selected)).isFile())throw new Error('Selected source is not a regular file.');
 }
 const chosen=selectedSource?{...profile,sourceRelativePath:selectedSource,caseId:undefined,
  trackedInputs:[...new Set([...profile.trackedInputs,selectedSource])]}:profile;
 const reader=await openProject({project:root,config,buildProfile:chosen.id,
  ...((existing||selectedSource)?{trustedBuildProfile:chosen,configureProfile:existing?.configure??(chosen.id===local.id?localCpuProfile(root).configure:undefined)}:{}),
  ...(selectedSource?{selectedSource,requestedCaseId:launch.caseId}:{})});
 let caseId=launch.caseId??'Sod',registrationPending=false;
 const executable=reader.snapshot().session.executable;
 if(executable?.error)throw new Error(executable.error);
 if(executable?.exists){
  const registry=await desktopRegistry(reader);
  if(selectedSource){
   const matches=registry.cases.filter(c=>c.inspection.sourceFile!==null&&path.resolve(root,c.inspection.sourceFile)===path.resolve(root,selectedSource));
   if(matches.length>1)throw new Error('Selected source has ambiguous compiled registration.');
   if(matches.length===1){
    caseId=registeredSourceCase(registry.cases,root,selectedSource,launch.caseId);
    registrationPending=matches[0].inspection.compiledSourceSha256!==(await fingerprint(root,selectedSource,'case-source')).sha256;
   }else registrationPending=true;
  }else if(!registry.cases.some(c=>c.caseId===caseId))throw new Error('Selected case is not registered by this binary.');
 }else registrationPending=true;
 return {root,reader,caseId,config,selectedSource,registrationPending};
}
