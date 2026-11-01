import assert from 'node:assert/strict';import {mkdtemp,mkdir,writeFile} from 'node:fs/promises';import os from 'node:os';import path from 'node:path';import {EventEmitter} from 'node:events';import {PassThrough} from 'node:stream';import type {BuildProfile} from '../src/host/contracts.ts';import {BuildRunner} from '../host/buildRunner.ts';
export function testBuildProfile(root:string):BuildProfile{return {id:'test-build',displayName:'Test build',managedSourceRoot:root,buildDirRelative:'build',target:'ARCH',outputBinaryRelative:'build/bin/ARCH',caseId:'Sod',sourceRelativePath:'case.cpp',parallelism:4,dependenciesComplete:false,trackedInputs:['case.cpp']};}
export async function fixture(){const root=await mkdtemp(path.join(os.tmpdir(),'arch-build-'));await mkdir(root+'/build/bin',{recursive:true});await writeFile(root+'/case.cpp','// fixture');await writeFile(root+'/build/CMakeCache.txt',`CMAKE_HOME_DIRECTORY:INTERNAL=${root}\nCMAKE_CACHEFILE_DIR:INTERNAL=${root}/build\n`);const p=testBuildProfile(root);return {root,p};}
export function fakeSpawn(root:string,code=0,output=true,delay=20){return ((_program:unknown,_args:unknown,options:unknown)=>{assert.equal((options as {shell:boolean}).shell,false);const child=new EventEmitter() as EventEmitter&{stdout:PassThrough;stderr:PassThrough};child.stdout=new PassThrough();child.stderr=new PassThrough();setTimeout(async()=>{if(output)await writeFile(root+'/build/bin/ARCH','fake compiled binary');child.stdout.end('compiler output\n');child.stderr.end('warning text\n');child.emit('close',code,null);},delay);return child;}) as never;}
export async function finished(r:BuildRunner){
 // Fake compilation still finishes real GNU subprocess/specs and file hashes.
 // Five seconds is a bounded test completion budget, not a product build SLA.
 const deadline=performance.now()+5_000;
 while(r.isActive()&&performance.now()<deadline)await new Promise(resolve=>setTimeout(resolve,10));
 assert.equal(r.isActive(),false,'Build fixture did not finish within its five-second test budget');
 return r.snapshot();
}
