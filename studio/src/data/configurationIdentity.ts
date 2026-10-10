import type {ConfigurationBuildScope,ConfigurationCase} from '../host/configurationContracts.ts';
import {sameBuildScope} from '../host/configurationContracts.ts';
import type {RealPreviewResult} from '../host/previewContracts.ts';
import type {ProjectSession} from '../host/contracts.ts';
import type {DiscoveryResponse,RegisteredCase} from '../host/workflowContracts.ts';
export type SelectedSourceModels={state:'unbound';cases:readonly RegisteredCase[]}|{state:'fixed';cases:readonly RegisteredCase[];caseId:string;message:string}|{state:'pending';cases:readonly RegisteredCase[];message:string};
/** Present the Host's source binding without granting execution or inferring a default case. */
export function selectedSourceModels(selectedSource:string|undefined,session:ProjectSession|null|undefined,discovery:DiscoveryResponse|null|undefined,requestedCaseId?:string):SelectedSourceModels {
 if(!selectedSource)return {state:'unbound',cases:discovery?.cases??[]};
 const pending=(reason:string):SelectedSourceModels=>({state:'pending',cases:[],message:'Selected source binding pending: '+reason});
 const source=session?.caseSource,binary=session?.executable;
 if(!session||!source?.exists||source.error||!source.sha256||source.relativePath!==selectedSource||!binary?.exists||binary.error||!binary.sha256||!discovery||discovery.projectId!==session.projectId||discovery.binarySha256!==binary.sha256)
  return pending('Waiting for matching source, binary and compiled case identities.');
 const absolute=session.projectRoot.replace(/\/$/,'')+'/'+selectedSource;
 // Like requireCompiledSourceCase, require a unique path before checking its digest.
 const matches=discovery.cases.filter(c=>c.inspection.sourceFile===absolute||c.inspection.sourceFile===selectedSource);
 if(matches.length!==1)return pending(matches.length?'More than one compiled case owns this source. Close this project and reopen a uniquely registered source.':'This source is not registered in the selected binary. Configure/Build, or close this project and reopen the intended source.');
 const matched=matches[0];
 if(matched.inspection.compiledSourceSha256!==source.sha256)return pending('Source differs from its compiled registration. Configure/Build before using this case.');
 if(requestedCaseId&&requestedCaseId!==matched.caseId)return pending('Requested model and selected source disagree. Close this project and reopen the intended source.');
 return {state:'fixed',cases:matches,caseId:matched.caseId,message:'Model is fixed to the selected source. Close this project and reopen a different source to change models.'};
}
export const modelSource:Record<ConfigurationCase,string>={Sod:'simulation/Sod/Sod.cpp',CellularDet:'simulation/Cellular/Cellular.cpp'};
export function pairingSuspicion(model:ConfigurationCase,filename:string):string|null {
 const name=filename.split(/[\\/]/).at(-1)??filename;
 if(model==='CellularDet'&&/^sod(?:[_. -]|$)/i.test(name))return 'Filename suggests this may be intended for Sod. Verify pairing.';
 if(model==='Sod'&&/^cellular/i.test(name))return 'Filename suggests this may be intended for CellularDet. Verify pairing.';
 return null;
}
export function previewMetadataMatches(result:RealPreviewResult|undefined,scope:ConfigurationBuildScope|null,model:ConfigurationCase){return !!result&&result.identity.caseId===model&&sameBuildScope(result.identity,scope);}
