import type {BuildProfile} from '../src/host/contracts.ts';
import type {ConfigureProfile} from './configureRunner.ts';
/** Host-owned CPU workflow; separate from existing scientific validation trees. */
export const LOCAL_CPU_PROFILE_ID='studio-cpu-release';
export function localCpuProfile(sourceRoot:string):{build:BuildProfile;configure:ConfigureProfile}{
 const buildDirRelative='build-studio-cpu';
 return {
  build:{id:LOCAL_CPU_PROFILE_ID,displayName:'Local CPU Release',managedSourceRoot:sourceRoot,
   buildDirRelative,target:'ARCH',outputBinaryRelative:buildDirRelative+'/bin/ARCH',
   parallelism:4,compilerDependencyMode:'ninja',dependenciesComplete:false,trackedInputs:['CMakeLists.txt','CMakePresets.json',buildDirRelative+'/CMakeCache.txt']},
  configure:{id:LOCAL_CPU_PROFILE_ID,sourceRoot,buildDirRelative,generator:'Ninja',
   definitions:{CMAKE_BUILD_TYPE:'Release',ARCH_ENABLE_CUDA:'OFF',ARCH_ENABLE_OPENMP:'ON',
    BUILD_TESTING:'OFF',ARCH_RUNTIME_OUTPUT_DIRECTORY:sourceRoot+'/'+buildDirRelative+'/bin'}}
 };
}
