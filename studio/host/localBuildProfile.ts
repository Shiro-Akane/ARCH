/**
 * Derive the local CPU recipe for the selected source root.
 * This is a profile description shared by launch/Configure; command execution
 * and successful-build evidence remain with their existing Host owners.
 */
import type {WindowsHostTools} from './hostProcess.ts';
import type {BuildProfile} from '../src/host/contracts.ts';
import type {ConfigureProfile} from './configureRunner.ts';
/** Host-owned CPU workflow; separate from existing scientific validation trees. */
export const LOCAL_CPU_PROFILE_ID='studio-cpu-release';
export function localCpuProfile(sourceRoot:string,windowsTools?:WindowsHostTools):{build:BuildProfile;configure:ConfigureProfile}{
 const buildDirRelative='build-studio-cpu';
 return {
  build:{id:LOCAL_CPU_PROFILE_ID,displayName:'Local CPU Release',managedSourceRoot:sourceRoot,
   ...(process.platform==='win32'&&windowsTools?{buildBackend:'cpu' as const,observedInputScope:'observed-build-inputs-v1' as const}:{}),
   registeredCases:['Sod','CellularDet'],buildDirRelative,target:'ARCH',outputBinaryRelative:buildDirRelative+'/bin/ARCH'+(process.platform==='win32'?'.exe':''),
   parallelism:4,compilerDependencyMode:'ninja',retainGnuLtoInputs:true,linkDependencyFile:'ARCH.link.d',dependenciesComplete:false,trackedInputs:['CMakeLists.txt','CMakePresets.json',buildDirRelative+'/CMakeCache.txt']},
  configure:{id:LOCAL_CPU_PROFILE_ID,sourceRoot,buildDirRelative,generator:'Ninja',...(windowsTools?{windowsTools}:{}),
   definitions:{...(windowsTools?.ninja?{CMAKE_MAKE_PROGRAM:windowsTools.ninja}:{}),...(windowsTools?.rc?{CMAKE_RC_COMPILER:windowsTools.rc}:{}),CMAKE_BUILD_TYPE:'Release',ARCH_ENABLE_CUDA:'OFF',ARCH_ENABLE_OPENMP:'ON',
    BUILD_TESTING:'OFF',ARCH_EMIT_LINK_DEPENDENCIES:'ON',ARCH_RETAIN_LTO_LINK_INPUTS:'ON',ARCH_RUNTIME_OUTPUT_DIRECTORY:sourceRoot+'/'+buildDirRelative+'/bin'}}
 };
}
