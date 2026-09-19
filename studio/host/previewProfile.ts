import {PREVIEW_BUILD_PROFILE} from './buildProfile.ts';
import type {PreviewProfile} from '../src/host/previewContracts.ts';
export const SOD_PREVIEW_PROFILE:PreviewProfile={id:'sod-initial-cpu',displayName:'Sod real initial state',buildProfileId:PREVIEW_BUILD_PROFILE.id,caseId:'Sod',dimension:1,defaultSampleCount:512,maxSampleCount:4096,runnerKind:'existing-arch-cli',configured:true};
