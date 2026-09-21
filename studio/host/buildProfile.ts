import path from 'node:path';
import {access,readFile,stat} from 'node:fs/promises';
import {constants} from 'node:fs';
import {createHash} from 'node:crypto';
import {checkedPath,selectedPath} from './files.ts';
import type {BuildProfile} from '../src/host/contracts.ts';
export const CMAKE='/usr/bin/cmake';
// This profile is Host-owned. No HTTP endpoint can create or edit profiles.
export const ARCH_PROFILE:BuildProfile={id:'arch-existing-cuda-release',displayName:'ARCH existing CUDA Release',managedSourceRoot:'/home/arch/projects/ARCH-linux',buildDirRelative:'build-cuda',target:'ARCH',outputBinaryRelative:'build-cuda/bin/ARCH',caseId:'Sod',sourceRelativePath:'simulation/Sod/Sod.cpp',parallelism:4,dependenciesComplete:false,trackedInputs:['simulation/Sod/Sod.cpp','src/main.cpp','src/core/ProblemRegistry.h','CMakeLists.txt','CMakePresets.json','cmake/Application.cmake','cmake/BuildOptions.cmake','cmake/CudaBackend.cmake','cmake/Dependencies.cmake','cmake/CustomNetworks.cmake','build-cuda/CMakeCache.txt']};
export function profileFingerprint(p:BuildProfile){return createHash('sha256').update(JSON.stringify(p)).digest('hex');}
export async function validateProfile(root:string,p:BuildProfile,cmake=CMAKE){
 if(process.platform!=='linux')throw new Error('Build requires the supported WSL/Linux host.');
 if(root!==p.managedSourceRoot)throw new Error('Profile source root does not match the managed project.');
 if(!/^[a-zA-Z0-9_-]+$/.test(p.id)||!p.displayName||!/^[-a-zA-Z0-9_.+]+$/.test(p.target)||p.target.startsWith('-'))throw new Error('Invalid fixed profile identity or target.');
 if(!Number.isInteger(p.parallelism)||p.parallelism<1||p.parallelism>28)throw new Error('Invalid fixed parallelism.');
 for(const rel of [p.buildDirRelative,p.outputBinaryRelative,...p.trackedInputs,...(p.sourceRelativePath?[p.sourceRelativePath]:[])])selectedPath(rel);
 if(p.trackedInputs.length>128||p.trackedInputs.some(x=>x.endsWith('.par')))throw new Error('Invalid tracked build inputs; runtime config is not compiled.');
 const dir=await checkedPath(root,p.buildDirRelative);if(!(await stat(dir)).isDirectory())throw new Error('Build directory is not configured.');
 const cachePath=await checkedPath(root,p.buildDirRelative+'/CMakeCache.txt');if((await stat(cachePath)).size>2*1024*1024)throw new Error('CMake cache exceeds limit.');
 const cache=await readFile(cachePath,'utf8');
 if(!cache.split(/\r?\n/).includes('CMAKE_HOME_DIRECTORY:INTERNAL='+root))throw new Error('CMake source root differs from managed source root.');
 if(!cache.split(/\r?\n/).includes('CMAKE_CACHEFILE_DIR:INTERNAL='+dir))throw new Error('CMake build directory binding differs.');
 const outputParent=path.dirname(p.outputBinaryRelative);await checkedPath(root,outputParent);
 await access(cmake,constants.X_OK);if(!(await stat(cmake)).isFile())throw new Error('CMake executable unavailable.');
 return dir;
}

// Separate fixed integration project. This never rebinds the existing CUDA tree.
export const PREVIEW_BUILD_PROFILE:BuildProfile={...ARCH_PROFILE,id:'arch-preview-cpu-integration',displayName:'ARCH Preview CPU integration (Debug)',managedSourceRoot:'/home/arch/projects/ARCH-phase2f-ui-contract-integration',registeredCases:['Sod','CellularDet'],caseId:'CellularDet',sourceRelativePath:'simulation/Cellular/Cellular.cpp',buildDirRelative:'build-preview-audit',outputBinaryRelative:'build-preview-audit/bin/ARCH',trackedInputs:ARCH_PROFILE.trackedInputs.filter(p=>p!=='build-cuda/CMakeCache.txt').concat(['build-preview-audit/CMakeCache.txt','src/core/StandardParameters.h','src/api/Configuration.h','src/api/Configuration.cpp','src/api/PresentationMetadata.cpp','src/api/LogCapture.h','src/driver/dispatch/PolicyDescriptor.h','src/driver/dispatch/ResolvedExecutionPlan.h','src/api/Preview.cpp','src/api/PreviewCommand.cpp','src/api/Preview.h','src/api/Json.h','src/core/InitialStateConversion.h','src/core/ProblemHelper.cpp','src/core/RuntimeParams.h','src/core/FileFingerprint.cpp','src/core/FileFingerprint.h','src/interface/GenericProblem.h','src/interface/ProblemGenerator.h','src/data/UserTypes.h','src/io/ConfigParser.h','cmake/tests/HostTests.cmake','src/api/ParameterMetadata.cpp','src/api/ParameterMetadata.h','src/interface/PreviewMetadata.h','src/data/GlobalDefs.h','src/api/Sampling.h','src/api/Response.h','src/core/ProblemHelper.h','src/grid/Grid.h','src/physics/constant/PhysicalConstants.h','src/amr/AmrDefines.h','simulation/Cellular/Cellular.cpp'])};
export const BUILD_PROFILES=[ARCH_PROFILE,PREVIEW_BUILD_PROFILE];
