import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,readFile,cp,rm} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';

const execute=promisify(execFile);
const cmake=process.env.ARCH_TEST_CMAKE??'cmake';
const repository=new URL('../../',import.meta.url);

for(const generator of process.platform==='win32'?['Ninja']:['Ninja','Unix Makefiles'])
test(`Studio ${generator} installs once per locked runtime, rebuilds assets independently and repairs a removed module tree`,async t=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch studio dependencies '));
 t.after(()=>rm(root,{recursive:true,force:true}));
 const studio=path.join(root,'studio');
 await mkdir(path.join(studio,'src'),{recursive:true});
 await mkdir(path.join(root,'cmake','studio'),{recursive:true});
 for(const name of ['Studio.cmake','WindowsTools.cmake','EnsureDependencies.cmake','arch-studio.in','arch-studio.cmd.in'])
  await cp(new URL('cmake/studio/'+name,repository),path.join(root,'cmake','studio',name));
 for(const name of ['index.html','tsconfig.json','vite.config.ts'])await writeFile(path.join(studio,name),'');
 await writeFile(path.join(studio,'package.json'),'{}');
 await writeFile(path.join(studio,'package-lock.json'),'{}');
 await writeFile(path.join(studio,'src','example.ts'),'first source');
 const npm=path.join(root,'npm-test.mjs');
 await writeFile(npm,`#!${process.execPath}
import {appendFileSync,existsSync,mkdirSync,readFileSync,rmSync,writeFileSync} from 'node:fs';
const [command,...args]=process.argv.slice(2);
if(command==='ci'){
 if(JSON.stringify(args)!==JSON.stringify(['--ignore-scripts','--no-audit','--no-fund']))throw new Error('Install flags changed');
 appendFileSync('calls.log','ci\\n');
 if(existsSync('fail-install'))process.exit(3);
 rmSync('node_modules',{recursive:true,force:true});
 for(const dir of ['typescript/bin','vite','electron'])mkdirSync('node_modules/'+dir,{recursive:true});
 for(const file of ['.package-lock.json','typescript/bin/tsc','vite/package.json','electron/package.json'])writeFileSync('node_modules/'+file,'{}');
}else if(command==='run'&&args[0]==='build'){
 appendFileSync('calls.log','build\\n');mkdirSync('dist',{recursive:true});writeFileSync('dist/index.html',readFileSync('src/example.ts'));
}else throw new Error('Unexpected npm command');
`,{mode:0o755});
 await writeFile(path.join(root,'CMakeLists.txt'),`cmake_minimum_required(VERSION 3.22)
project(StudioDependencies NONE)
if(WIN32)
 foreach(target ARCH arch_windows_process arch_windows_build_publish)
  add_executable(\${target} IMPORTED)
  set_target_properties(\${target} PROPERTIES IMPORTED_LOCATION "\${ARCH_STUDIO_NODE}")
 endforeach()
 set(CMAKE_RC_COMPILER "\${ARCH_STUDIO_NODE}")
 set(ARCH_STUDIO_OBJDUMP "\${ARCH_STUDIO_NODE}")
else()
 add_custom_target(ARCH)
endif()
set(ARCH_RUNTIME_OUTPUT_DIRECTORY "\${CMAKE_BINARY_DIR}/bin")
include(cmake/studio/Studio.cmake)
`);
 const configure=async(build:string)=>execute(cmake,['-S',root,'-B',path.join(root,build),'-G',generator,'-DARCH_BUILD_STUDIO=ON','-DARCH_STUDIO_NODE='+process.execPath,
  ...(process.platform==='win32'?['-DARCH_STUDIO_NPM_CLI='+npm]:['-DARCH_STUDIO_NPM='+npm]),
  ...(generator==='Ninja'&&process.env.ARCH_TEST_NINJA?['-DCMAKE_MAKE_PROGRAM='+process.env.ARCH_TEST_NINJA]:[])],{timeout:30000});
 const build=(directory='build-a')=>execute(cmake,['--build',path.join(root,directory),'--target','arch-studio-assets'],{timeout:30000});
 const calls=async()=>((await readFile(path.join(studio,'calls.log'),'utf8')).trim().split('\n'));
 await configure('build-a');await build();assert.deepEqual(await calls(),['ci','build']);
 await build();assert.deepEqual(await calls(),['ci','build'],'unchanged build should neither install nor rebuild');
 await writeFile(path.join(studio,'src','example.ts'),'edited source');await build();
 assert.deepEqual(await calls(),['ci','build','build']);assert.equal(await readFile(path.join(studio,'dist/index.html'),'utf8'),'edited source');
 await writeFile(path.join(studio,'package-lock.json'),'{"revision":1}');await build();
 assert.deepEqual(await calls(),['ci','build','build','ci','build']);
 await rm(path.join(studio,'node_modules'),{recursive:true});await build();
 assert.deepEqual(await calls(),['ci','build','build','ci','build','ci','build']);
 await configure('build-b');await build('build-b');
 assert.equal((await calls()).filter(x=>x==='ci').length,3,'build trees share the installed dependencies');
 await writeFile(path.join(studio,'package-lock.json'),'{"revision":2}');
 await Promise.all([build('build-a'),build('build-b')]);
 assert.equal((await calls()).filter(x=>x==='ci').length,4,'concurrent build trees serialize one install');
 await writeFile(path.join(studio,'fail-install'),'fail');await writeFile(path.join(studio,'package-lock.json'),'{"revision":3}');
 await assert.rejects(build(),/Command failed/);
 await rm(path.join(studio,'fail-install'));await build();
 assert.equal((await calls()).filter(x=>x==='ci').length,6,'failed install is retried');
});
