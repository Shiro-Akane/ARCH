import {PREVIEW_BUILD_PROFILE} from './buildProfile.ts';
import {samplingForDimension} from '../src/host/previewContracts.ts';
import type {ModelCapability,PreviewProfile} from '../src/host/previewContracts.ts';
export const SOD_PREVIEW_PROFILE:PreviewProfile={id:'sod-initial-cpu',displayName:'Sod real initial state',buildProfileId:PREVIEW_BUILD_PROFILE.id,caseId:'Sod',dimension:1,defaultSampleCount:512,maxSampleCount:4096,runnerKind:'existing-arch-cli',configured:true};

export const CELLULAR_PREVIEW_PROFILE:PreviewProfile={id:'cellular-initial-cpu',displayName:'CellularDet real 2D initial state',buildProfileId:PREVIEW_BUILD_PROFILE.id,caseId:'CellularDet',dimension:2,defaultSampleCount:16384,maxSampleCount:65536,defaultShape:[128,128],maxPerAxis:256,runnerKind:'existing-arch-cli',configured:true};
export const PREVIEW_PROFILES=[SOD_PREVIEW_PROFILE,CELLULAR_PREVIEW_PROFILE];

/** Profiles come only from the selected binary; IDs never encode a command. */
export function profilesFromModels(models:ModelCapability[],buildProfileId:string):PreviewProfile[]{
 return models.flatMap(model=>model.dimensions.map(dimension=>{
  const sampling=samplingForDimension(model,dimension);
  if(!sampling)throw new Error('Missing dimensional sampling capability.');
  const id=model.caseId==='Sod'&&dimension===1?'sod-initial-cpu'
   :model.caseId==='CellularDet'&&dimension===2?'cellular-initial-cpu'
   :'initial-cpu-'+encodeURIComponent(model.caseId)+'-'+dimension+'d';
  return {id,displayName:model.caseId+' real '+dimension+'D initial state',buildProfileId,
   caseId:model.caseId,dimension:dimension as 1|2|3,
   defaultSampleCount:sampling.defaultShape.reduce((a,b)=>a*b,1),
   maxSampleCount:sampling.maxTotalSamples,
   ...(dimension>1?{defaultShape:sampling.defaultShape,maxPerAxis:sampling.maxPerAxis}:{}),
   runnerKind:'existing-arch-cli' as const,configured:true};
 }));
}
