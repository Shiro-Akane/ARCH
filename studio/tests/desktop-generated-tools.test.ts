import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,readFile,writeFile,rm} from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {fileURLToPath} from 'node:url';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {loadWindowsTools} from '../host/windows-tools.mjs';

const execute=promisify(execFile);
test('native CMake generates one private tool and runtime environment for Studio and CLI',
 {skip:process.platform!=='win32'},async t=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch tools % & '));
 t.after(()=>rm(root,{recursive:true,force:true}));
 const forward=(value:string)=>value.replaceAll('\\','/');
 const node=forward(process.execPath),build=path.join(root,'build');
 const dirs=Object.fromEntries(['INCLUDE','LIB','LIBPATH'].map(key=>[key,path.join(root,key)]));
 for(const dir of Object.values(dirs))await mkdir(dir);
 const runtime=path.join(root,'runtime');await mkdir(runtime);
 const libraries=['cudss.dll','cublas.dll','cublasLt.dll'].map(name=>path.join(runtime,name));
 for(const file of libraries)await writeFile(file,'fixture runtime');
 await writeFile(path.join(root,'CMakeLists.txt'),`cmake_minimum_required(VERSION 3.22)
project(StudioTools NONE)
foreach(target ARCH arch_windows_process arch_windows_build_publish)
 add_executable(\${target} IMPORTED)
 set_target_properties(\${target} PROPERTIES IMPORTED_LOCATION [=[${node}]=])
endforeach()
set(ARCH_RUNTIME_OUTPUT_DIRECTORY "\${CMAKE_BINARY_DIR}/bin")
set(ARCH_STUDIO_NODE [=[${node}]=])
set(CMAKE_C_COMPILER [=[${node}]=])
set(CMAKE_CXX_COMPILER [=[${node}]=])
set(CMAKE_RC_COMPILER [=[${node}]=])
# The explicit inspector wins even when the compiler names another objdump.
set(CMAKE_OBJDUMP [=[${forward(path.join(root,'objdump.exe'))}]=])
set(ARCH_STUDIO_OBJDUMP [=[${node}]=])
set(ARCH_PREPARED_RUNTIME_DIRS [=[${forward(runtime)}]=])
set(ARCH_ENABLE_CUDA ON)
option(ARCH_ENABLE_CUDSS "Use fixture cuDSS" ON)
set(CuDSS_FOUND TRUE)
set(CuDSS_RUNTIME_LIBRARY [=[${forward(libraries[0])}]=])
set(CUDA_cublas_RUNTIME_LIBRARY [=[${forward(libraries[1])}]=])
set(CUDA_cublasLt_RUNTIME_LIBRARY [=[${forward(libraries[2])}]=])
include([=[${forward(fileURLToPath(new URL('../../cmake/project/WindowsRuntime.cmake',import.meta.url)))}]=])
include([=[${forward(fileURLToPath(new URL('../../cmake/studio/WindowsTools.cmake',import.meta.url)))}]=])
`);
 const cmake=process.env.ARCH_TEST_CMAKE??'cmake';
 await execute(cmake,['-S',root,'-B',build,'-G','Ninja',
  ...(process.env.ARCH_TEST_NINJA?['-DCMAKE_MAKE_PROGRAM='+process.env.ARCH_TEST_NINJA]:[])],
  {env:{...process.env,...dirs},timeout:30000});
 const value=await loadWindowsTools(await readFile(path.join(build,'studio','windows-tools.json'),'utf8'));
 assert.equal(value.objdump,node);assert.equal(value.rc,node);
 assert.deepEqual(value.compilerEnvironment,Object.fromEntries(Object.entries(dirs).map(([key,value])=>[key,forward(value)])));
 assert.deepEqual(value.runtimeLibraries,libraries.map(forward));
 assert.ok(value.path.split(';').includes(forward(runtime)));
 const command=`call "${path.join(build,'bin','arch-env.cmd')}" && set PATH && set INCLUDE`;
 const result=await execute(process.env.ComSpec??'cmd.exe',['/d','/s','/c',command],{encoding:'utf8',timeout:10000,windowsVerbatimArguments:true});
 const cliPath=result.stdout.split(/\r?\n/).find(line=>/^PATH=/i.test(line))?.slice(5);
 assert.ok(cliPath);
 const studioDirectories=value.path.split(';').map(dir=>path.win32.normalize(dir).toLowerCase());
 for(const dir of cliPath.split(';'))assert.ok(studioDirectories.includes(path.win32.normalize(dir).toLowerCase()),dir);
 assert.ok(result.stdout.includes('INCLUDE='+forward(dirs.INCLUDE)));
 await rm(libraries[0]);
 await assert.rejects(execute(process.env.ComSpec??'cmd.exe',['/d','/s','/c',command],{windowsVerbatimArguments:true}),error=>
  error instanceof Error&&'stderr' in error&&String(error.stderr).includes('Required Windows runtime DLL is unavailable:'));
 await assert.rejects(loadWindowsTools(JSON.stringify(value)),/Required Windows runtime DLL/);
 // Removing an optional provider also removes its runtime prerequisites.
 await execute(cmake,['-S',root,'-B',build,'-DARCH_ENABLE_CUDSS=OFF'],
  {env:{...process.env,...dirs},timeout:30000});
 const withoutCuDss=await loadWindowsTools(await readFile(path.join(build,'studio','windows-tools.json'),'utf8'));
 assert.deepEqual(withoutCuDss.runtimeLibraries,[]);
 await execute(process.env.ComSpec??'cmd.exe',['/d','/s','/c',command],{windowsVerbatimArguments:true});
});

test('manual native Core retains selected HDF5/OpenMP runtimes without preparation, Node or objdump',
 {skip:process.platform!=='win32'},async t=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch core runtime '));
 t.after(()=>rm(root,{recursive:true,force:true}));
 const forward=(value:string)=>value.replaceAll('\\','/');
 const binary=path.join(root,'bin','ARCH.exe'),compiler=path.join(root,'compiler','cc.exe');
 for(const file of [binary,compiler]){await mkdir(path.dirname(file),{recursive:true});await writeFile(file,'fixture executable');}
 const hdfRoot=path.join(root,'hdf-root'),prefix=path.join(root,'prefix');
 const selectedLibrary=path.join(root,'selected-hdf','lib','hdf5.lib');
 const importedDll=path.join(root,'imported-hdf','runtime','hdf5_cpp.dll');
 const redist=path.join(root,'redist'),openmp=path.join(redist,'debug_nonredist','x64','Microsoft.VC143.OpenMP.LLVM');
 const runtimeDirs=[path.join(hdfRoot,'bin'),path.join(prefix,'bin'),path.join(root,'selected-hdf','bin'),path.dirname(importedDll),openmp];
 for(const dir of runtimeDirs)await mkdir(dir,{recursive:true});
 await mkdir(path.dirname(selectedLibrary),{recursive:true});
 for(const file of [selectedLibrary,importedDll,path.join(openmp,'libomp140.x86_64.dll')])await writeFile(file,'fixture library');
 const build=path.join(root,'build');
 await writeFile(path.join(root,'CMakeLists.txt'),`cmake_minimum_required(VERSION 3.22)
project(CoreRuntime NONE)
add_executable(ARCH IMPORTED)
set_target_properties(ARCH PROPERTIES IMPORTED_LOCATION [=[${forward(binary)}]=])
set(CMAKE_C_COMPILER [=[${forward(compiler)}]=])
set(CMAKE_CXX_COMPILER [=[${forward(compiler)}]=])
set(ARCH_RUNTIME_OUTPUT_DIRECTORY "\${CMAKE_BINARY_DIR}/bin")
set(ARCH_BUILD_STUDIO OFF)
set(ARCH_STUDIO_NODE "C:/unavailable-node/node.exe")
set(ARCH_STUDIO_OBJDUMP "C:/unavailable-inspector/objdump.exe")
set(HDF5_ROOT [=[${forward(hdfRoot)}]=])
set(CMAKE_PREFIX_PATH [=[${forward(prefix)};${forward(path.join(root,'absent-prefix'))}]=])
set(HDF5_LIBRARIES [=[${forward(selectedLibrary)}]=])
add_library(selected_hdf_cpp SHARED IMPORTED)
set_target_properties(selected_hdf_cpp PROPERTIES IMPORTED_LOCATION_RELEASE [=[${forward(importedDll)}]=])
set(HDF5_CXX_LIBRARIES selected_hdf_cpp)
set(CMAKE_BUILD_TYPE Release)
set(MSVC TRUE)
set(ARCH_ENABLE_OPENMP ON)
include([=[${forward(fileURLToPath(new URL('../../cmake/project/WindowsRuntime.cmake',import.meta.url)))}]=])
include([=[${forward(fileURLToPath(new URL('../../cmake/studio/Studio.cmake',import.meta.url)))}]=])
`);
 const systemRoot=process.env.SystemRoot??'C:/Windows';
 const cmake=process.env.ARCH_TEST_CMAKE??(await execute('where.exe',['cmake'])).stdout.trim().split(/\r?\n/)[0];
 const ninja=process.env.ARCH_TEST_NINJA??(await execute('where.exe',['ninja'])).stdout.trim().split(/\r?\n/)[0];
 await execute(cmake,['-S',root,'-B',build,'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+ninja],
  {env:{...process.env,PATH:systemRoot+'/System32;'+systemRoot,VCToolsRedistDir:redist},timeout:30000});
 const script=await readFile(path.join(build,'bin','arch-env.cmd'),'utf8');
 assert.ok(script.includes(forward(path.dirname(compiler))));
 assert.ok(script.includes(forward(path.dirname(binary))));
 for(const dir of runtimeDirs)assert.ok(script.includes(forward(dir)),dir);
 assert.doesNotMatch(script,/unavailable-node|unavailable-inspector|absent-prefix/);
 await assert.rejects(readFile(path.join(build,'studio','windows-tools.json')),/ENOENT/);
 const command=`call "${path.join(build,'bin','arch-env.cmd')}" && set PATH`;
 await execute(process.env.ComSpec??'cmd.exe',['/d','/s','/c',command],{windowsVerbatimArguments:true});
});
