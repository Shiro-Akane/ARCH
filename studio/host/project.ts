import path from 'node:path';import {randomUUID} from 'node:crypto';
import {fingerprint,projectRoot,selectedPath} from './files.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
import type {ProjectSnapshot,ProjectFileRef} from '../src/host/contracts.ts';
export interface ProjectOptions { project:string; case?:string; config?:string; binary?:string }
export async function openProject(options:ProjectOptions) {
 const root=await projectRoot(options.project);
 for(const value of [options.case,options.config,options.binary])if(value!==undefined)selectedPath(value);
 const read=async(value:string|undefined,kind:ProjectFileRef['kind'])=>value===undefined?undefined:await fingerprint(root,value,kind);
 const [caseSource,parameterFile,executable]=await Promise.all([read(options.case,'case-source'),read(options.config,'parameter'),read(options.binary,'executable')]);
 const state=(file:ProjectFileRef|undefined)=>!file||file.error?'unknown' as const:!file.exists?'missing' as const:'available' as const;
 let result:ProjectSnapshot={host:{protocolVersion:PROTOCOL_VERSION,hostKind:'local',platform:process.platform,projectRoot:root,capabilities:{readProject:true,writeConfig:false,build:false,preview:false,watchFiles:false}},session:{projectId:randomUUID(),displayName:path.basename(root),projectRoot:root,caseSource,parameterFile,executable,sourceState:state(caseSource),configFileState:state(parameterFile),binaryState:state(executable),mapping:'unknown',metadata:'unavailable',openedAt:new Date().toISOString(),refreshedAt:new Date().toISOString()}};
 const baseline=structuredClone(result.session);
 const changed=(a:ProjectFileRef|undefined,b:ProjectFileRef|undefined)=>Boolean(a&&b&&(a.exists!==b.exists||a.sha256!==b.sha256||a.size!==b.size||a.modifiedTime!==b.modifiedTime));
 async function refresh(){
  if(await projectRoot(root)!==root)throw new Error('Project root identity changed');
  const next=await Promise.all([read(options.case,'case-source'),read(options.config,'parameter'),read(options.binary,'executable')]);
  const keys=['caseSource','parameterFile','executable'] as const;
  const session=structuredClone(result.session);
  for(let i=0;i<keys.length;i++){
   const key=keys[i],file=next[i],previous=result.session[key];
   session[key]=file?.error&&previous?{...previous,error:file.error}:file;
   const current=session[key];if(current&&!current.error)current.changed=changed(baseline[key],current);
  }
  session.sourceState=state(session.caseSource);if(session.sourceState==='available'&&session.caseSource?.changed)session.sourceState='changed';
  session.configFileState=state(session.parameterFile);if(session.configFileState==='available'&&session.parameterFile?.changed)session.configFileState='changed-externally';
  session.binaryState=state(session.executable);session.refreshedAt=new Date().toISOString();
  result={host:result.host,session};return structuredClone(result);
 }
 let pending:Promise<ProjectSnapshot>|undefined;
 return {snapshot:()=>structuredClone(result),refresh:()=>{if(!pending)pending=refresh().finally(()=>{pending=undefined;});return pending;}};
}
