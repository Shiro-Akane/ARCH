import type {ConfigurationBuildScope,ConfigurationCase} from '../host/configurationContracts.ts';
import {sameBuildScope} from '../host/configurationContracts.ts';
import type {RealPreviewResult} from '../host/previewContracts.ts';
export const modelSource:Record<ConfigurationCase,string>={Sod:'simulation/Sod/Sod.cpp',CellularDet:'simulation/Cellular/Cellular.cpp'};
export function pairingSuspicion(model:ConfigurationCase,filename:string):string|null {
 const name=filename.split(/[\\/]/).at(-1)??filename;
 if(model==='CellularDet'&&/^sod(?:[_. -]|$)/i.test(name))return 'Filename suggests this may be intended for Sod. Verify pairing.';
 if(model==='Sod'&&/^cellular/i.test(name))return 'Filename suggests this may be intended for CellularDet. Verify pairing.';
 return null;
}
export function previewMetadataMatches(result:RealPreviewResult|undefined,scope:ConfigurationBuildScope|null,model:ConfigurationCase){return !!result&&result.identity.caseId===model&&sameBuildScope(result.identity,scope);}
