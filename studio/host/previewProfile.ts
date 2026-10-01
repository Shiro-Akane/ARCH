import {PREVIEW_BUILD_PROFILE} from './buildProfile.ts';
import type {PreviewProfile} from '../src/host/previewContracts.ts';
export const SOD_PREVIEW_PROFILE:PreviewProfile={id:'sod-initial-cpu',displayName:'Sod real initial state',buildProfileId:PREVIEW_BUILD_PROFILE.id,caseId:'Sod',dimension:1,defaultSampleCount:512,maxSampleCount:4096,runnerKind:'existing-arch-cli',configured:true};

export const CELLULAR_PREVIEW_PROFILE:PreviewProfile={id:'cellular-initial-cpu',displayName:'CellularDet real 2D initial state',buildProfileId:PREVIEW_BUILD_PROFILE.id,caseId:'CellularDet',dimension:2,defaultSampleCount:16384,maxSampleCount:65536,defaultShape:[128,128],maxPerAxis:256,runnerKind:'existing-arch-cli',configured:true};
export const PREVIEW_PROFILES=[SOD_PREVIEW_PROFILE,CELLULAR_PREVIEW_PROFILE];
