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
 if(p.retainGnuLtoInputs!==undefined&&typeof p.retainGnuLtoInputs!=='boolean')throw new Error('Invalid Host-owned LTO retention mode.');
 if(p.retainGnuLtoInputs&&(p.compilerDependencyMode!=='ninja'||!p.linkDependencyFile))throw new Error('LTO retention requires compiler and linker dependency capture.');
 if(p.linkDependencyFile)selectedPath(p.linkDependencyFile);
 for(const rel of [p.buildDirRelative,p.outputBinaryRelative,...p.trackedInputs,...(p.sourceRelativePath?[p.sourceRelativePath]:[])])selectedPath(rel);
 if(p.trackedInputs.length>128||p.trackedInputs.some(x=>x.endsWith('.par')))throw new Error('Invalid tracked build inputs; runtime config is not compiled.');
 for(const rel of new Set([...p.trackedInputs,...(p.sourceRelativePath?[p.sourceRelativePath]:[])])){
  try{if(!(await stat(await checkedPath(root,rel))).isFile())throw new Error('Not a file');}
  catch{throw new Error('Tracked input unavailable; profile migration required: '+rel);}
 }
 const dir=await checkedPath(root,p.buildDirRelative);if(!(await stat(dir)).isDirectory())throw new Error('Build directory is not configured.');
 const cachePath=await checkedPath(root,p.buildDirRelative+'/CMakeCache.txt');if((await stat(cachePath)).size>2*1024*1024)throw new Error('CMake cache exceeds limit.');
 const cache=await readFile(cachePath,'utf8');
 if(!cache.split(/\r?\n/).includes('CMAKE_HOME_DIRECTORY:INTERNAL='+root))throw new Error('CMake source root differs from managed source root.');
 if(!cache.split(/\r?\n/).includes('CMAKE_CACHEFILE_DIR:INTERNAL='+dir))throw new Error('CMake build directory binding differs.');
 const outputParent=path.dirname(p.outputBinaryRelative);await checkedPath(root,outputParent);
 await access(cmake,constants.X_OK);if(!(await stat(cmake)).isFile())throw new Error('CMake executable unavailable.');
 return dir;
}

// Fixed Phase 2H CPU project. Legacy CUDA and sealed Phase 2G trees stay separate.
// Explicit reviewed inputs are not a complete transitive dependency graph.
export const PREVIEW_BUILD_PROFILE:BuildProfile={
 ...ARCH_PROFILE,
 id:'arch-phase3a-cpu-integration',
 displayName:'ARCH Phase 3A mainline CPU (Debug)',
 managedSourceRoot:'/home/arch/projects/ARCH-mainline',
 registeredCases:['Sod','CellularDet'],
 caseId:'CellularDet',
 sourceRelativePath:'simulation/Cellular/Cellular.cpp',
 buildDirRelative:'build-phase3a-cpu',
 outputBinaryRelative:'build-phase3a-cpu/bin/ARCH',
 parallelism:8,
 dependenciesComplete:false,
 trackedInputs:[
  'simulation/Sod/Sod.cpp',
  'src/main.cpp',
  'src/core/problem/ProblemRegistry.h',
  'CMakeLists.txt',
  'CMakePresets.json',
  'cmake/project/Application.cmake',
  'cmake/project/BuildOptions.cmake',
  'cmake/cuda/CudaBackend.cmake',
  'cmake/dependencies/Dependencies.cmake',
  'cmake/CustomNetworks.cmake',
  'build-phase3a-cpu/CMakeCache.txt',
  'src/core/config/StandardParameters.h',
  'src/api/Configuration.h',
  'src/api/configuration/Configuration.cpp',
  'src/api/configuration/PresentationMetadata.cpp',
  'src/api/protocol/LogCapture.h',
  'src/driver/dispatch/PolicyDescriptor.h',
  'src/driver/dispatch/capability/ResolvedExecutionPlan.h',
  'src/api/preview/Preview.cpp',
  'src/api/preview/PreviewCommand.cpp',
  'src/api/Preview.h',
  'src/api/protocol/Json.h',
  'src/core/problem/InitialStateConversion.h',
  'src/core/problem/ProblemHelper.cpp',
  'src/core/config/RuntimeParams.h',
  'src/core/files/FileFingerprint.cpp',
  'src/core/files/FileFingerprint.h',
  'src/interface/GenericProblem.h',
  'src/interface/ProblemGenerator.h',
  'src/data/UserTypes.h',
  'src/io/ConfigParser.h',
  'cmake/tests/HostTests.cmake',
  'src/api/configuration/ParameterMetadata.cpp',
  'src/api/configuration/ParameterMetadata.h',
  'src/interface/PreviewMetadata.h',
  'src/data/GlobalDefs.h',
  'src/api/preview/Sampling.h',
  'src/api/protocol/Response.h',
  'src/core/problem/ProblemHelper.h',
  'src/grid/Grid.h',
  'src/physics/constant/PhysicalConstants.h',
  'src/amr/topology/AmrDefines.h',
  'simulation/Cellular/Cellular.cpp',
  'src/amr/refinement/RefinementThermodynamics.h',
  'src/api/ApplicationContract.h',
  'src/api/inspection/CaseInspection.cpp',
  'src/api/CaseInspection.h',
  'src/api/inspection/CaseUnitEvidence.cpp',
  'src/api/inspection/Discovery.cpp',
  'src/api/preview/InitialMesh.h',
  'src/api/session/InitialSampleCache.h',
  'src/api/configuration/ParameterPresentation.h',
  'src/api/session/PreviewSession.cpp',
  'src/api/PreviewSession.h',
  'src/api/protocol/Progress.h',
  'src/api/protocol/RequestInput.h',
  'src/api/preview/ResourceEstimates.cpp',
  'src/api/preview/ResourceEstimates.h',
  'src/api/session/SessionInput.h',
  'src/api/preview/StateSnapshot.cpp',
  'src/api/preview/StateSnapshot.h',
  'src/api/configuration/ValueDomain.h',
  'src/api/resources/WorkerLimits.cpp',
  'src/api/resources/WorkerLimits.h',
  'src/core/files/InspectionSources.h',
  'src/core/config/UserInterface.h',
  'src/core/files/VerifiedFileCache.h',
  'src/driver/Driver.h',
  'src/driver/initialization/InitialMesh.h',
  'src/driver/SolverDispatch.cpp',
  'src/physics/eos/IdealGas.h',
  'src/physics/eos/InspectionEosCache.h',
  'src/physics/eos/eosdispatch.h',
  'src/physics/network/timmes_common/TimmesNetworkSupport.h',
  'src/physics/species/Species.h',
  'include/UserInterface.h',
  'cmake/project/SelectIpoLinker.cmake',
  'src/physics/eos/eosdispatch.cpp',
  'src/physics/gravity/GravityExecution.cpp',
  'src/physics/gravity/GravityBoundary.cpp',
  'src/physics/gravity/self/SelfGravity.cpp',
  'src/physics/gravity/self/GravityWorkspace.cpp',
  'src/numerics/elliptic/CartesianPoisson.cpp',
  'src/numerics/elliptic/CompositePoisson.cpp',
  'src/numerics/multigrid/HostMultigrid.cpp',
  'src/numerics/multigrid/CompositeMultigrid.cpp',
  'src/numerics/multigrid/CompositeExecution.cpp',
  'src/driver/runtime/DriverRuntime.cpp',
  'src/driver/runtime/DriverBoundary.cpp',
  'src/driver/runtime/DriverRegrid.cpp',
  'src/driver/io/DriverIO.cpp',
  'src/driver/stages/GravityStage.cpp',
  'src/amr/elliptic/EllipticMeshAdapter.cpp',
  'include/GlobalDefs.h',
 ],
};
// Reviewed, existing unique O7 CPU tree. Opening validates its original bindings;
// it never configures/rebinds the tree or invents a successful Build manifest.
// This explicit input set is incomplete: freshness remains unknown without evidence.
export const COMPUTE_OPTIM_CPU_PROFILE:BuildProfile={
 ...PREVIEW_BUILD_PROFILE,
 id:'arch-compute-optim-existing-cpu',
 displayName:'ARCH compute/optim existing CPU Release',
 managedSourceRoot:'/home/arch/projects/ARCH-compute-optim',
 buildDirRelative:'build-cpu',
 outputBinaryRelative:'build-cpu/bin/ARCH',
 caseId:'GravityBox',
 sourceRelativePath:'simulation/GravityBox/GravityBox.cpp',
 registeredCases:undefined,
 parallelism:28,
 trackedInputs:PREVIEW_BUILD_PROFILE.trackedInputs
  .filter(input=>input!=='build-phase3a-cpu/CMakeCache.txt')
  .concat(['build-cpu/CMakeCache.txt','simulation/GravityBox/GravityBox.cpp','src/physics/diagnostics/JeansDiagnostics.h']),
};
export const BUILD_PROFILES=[ARCH_PROFILE,PREVIEW_BUILD_PROFILE,COMPUTE_OPTIM_CPU_PROFILE];
