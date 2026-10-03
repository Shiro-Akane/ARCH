import type {InspectionResponse} from '../host/configurationContracts.ts';
import type {PreviewProfile} from '../host/previewContracts.ts';
export function previewProfileForConfiguration(profiles:PreviewProfile[],inspection:InspectionResponse|null,
 text:string|undefined,inspectedText:string|undefined,projectId:string,caseId:string,buildId?:string,binarySha256?:string){
 if(!inspection||text!==inspectedText||inspection.identity.projectId!==projectId
  ||inspection.identity.caseId!==caseId||inspection.identity.buildId!==buildId
  ||inspection.identity.binarySha256!==binarySha256)return;
 const dimension=inspection.core.coordinates?.dimension;
 return profiles.find(p=>p.caseId===caseId&&p.dimension===dimension);
}
