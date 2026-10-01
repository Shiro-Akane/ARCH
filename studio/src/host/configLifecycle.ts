import {loadPar} from '../state/parState.ts';
import type {ParState} from '../state/parState.ts';
import type {ConfigLifecycleState,ConfigReadResponse,FileFingerprint,ProjectSnapshot} from './contracts.ts';
export function sameFingerprint(a:FileFingerprint,b:FileFingerprint){return a.sha256===b.sha256&&a.size===b.size&&a.modifiedTime===b.modifiedTime;}
export function associateConfig(read:ConfigReadResponse):{state:ParState;lifecycle:ConfigLifecycleState}{
 return {state:loadPar(read.relativePath,read.text),lifecycle:{association:{projectId:read.projectId,relativePath:read.relativePath,loadedFingerprint:read.fingerprint},loadedFingerprint:read.fingerprint,savedFingerprint:read.fingerprint,diskState:'in-sync'}};
}
export function diskStatus(lifecycle:ConfigLifecycleState,project:ProjectSnapshot):ConfigLifecycleState['diskState'] {
 const association=lifecycle.association,file=project.session.parameterFile;
 if(!association||association.projectId!==project.session.projectId||file?.relativePath!==association.relativePath)return 'unknown';
 if(file.error)return 'read-error';if(!file.exists)return 'missing';
 if(!lifecycle.savedFingerprint||file.sha256===undefined||file.size===undefined||file.modifiedTime===undefined)return 'unknown';
 return sameFingerprint(lifecycle.savedFingerprint,{sha256:file.sha256,size:file.size,modifiedTime:file.modifiedTime})?'in-sync':'changed-externally';
}
