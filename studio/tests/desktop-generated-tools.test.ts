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
# A selected MSVC dumpbin must not become the GNU-compatible PE inspector.
set(CMAKE_OBJDUMP [=[${forward(path.join(root,'dumpbin.exe'))}]=])
set(ARCH_STUDIO_OBJDUMP [=[${node}]=])
set(ARCH_PREPARED_RUNTIME_DIRS [=[${forward(runtime)}]=])
set(ARCH_ENABLE_CUDA ON)
option(ARCH_ENABLE_CUDSS "Use fixture cuDSS" ON)
set(CuDSS_FOUND TRUE)
set(CuDSS_RUNTIME_LIBRARY [=[${forward(libraries[0])}]=])
set(CUDA_cublas_RUNTIME_LIBRARY [=[${forward(libraries[1])}]=])
set(CUDA_cublasLt_RUNTIME_LIBRARY [=[${forward(libraries[2])}]=])
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
 assert.ok(result.stdout.includes('PATH='+value.path));
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
