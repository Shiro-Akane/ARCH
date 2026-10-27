/** Real OS lifecycle tests; native Windows requires the CMake-built supervisor. */
import test from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import type {ChildProcess} from 'node:child_process';
import {copyFile,mkdtemp,readFile,rm,writeFile,access} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {spawnOwned,terminateOwned,windowsProcessIdentity,requireWindowsProcess} from '../host/hostProcess.ts';
import type {WindowsHostTools} from '../host/hostProcess.ts';
import {ConfigureRunner} from '../host/configureRunner.ts';

const windows=process.platform==='win32';
const tools:WindowsHostTools={cmake:process.env.ARCH_TEST_CMAKE??'',supervisor:process.env.ARCH_TEST_SUPERVISOR??'',path:process.env.PATH??''};
const native={skip:!windows,timeout:30000};
const pause=(ms:number)=>new Promise(r=>setTimeout(r,ms));
function closed(child:ChildProcess){
 return new Promise<{code:number|null;signal:string|null;out:string;err:string}>((resolve,reject)=>{
  let out='',err='';child.stdout?.setEncoding('utf8');child.stderr?.setEncoding('utf8');
  child.stdout?.on('data',s=>out+=s);child.stderr?.on('data',s=>err+=s);
  child.once('error',reject);child.once('close',(code,signal)=>resolve({code,signal,out,err}));
 });
}
async function until(check:()=>Promise<boolean>|boolean){
 const end=Date.now()+10000;
 while(Date.now()<end){if(await check())return;await pause(20);}
 throw new Error('Owned process did not reach its expected lifecycle boundary.');
}
function alive(pid:number){try{return windowsProcessIdentity(tools,pid).alive;}catch{return false;}}
function owned(args:string[]){return spawnOwned(process.execPath,args,{stdio:['pipe','pipe','pipe'],env:process.env},windows?tools:undefined);}

test('owned argv/stdin/stdout/stderr preserve Unicode, empty and shell-looking tokens', {timeout:15000},async()=>{
 const root=await mkdtemp(path.join(tmpdir(),'ARCH 中文 space-'));
 try{
  const program=windows?path.join(root,'node copy.exe'):process.execPath;
  if(windows)await copyFile(process.execPath,program);
  const tokens=['','中文 space','a"b','tail\\','& echo UNEXPECTED','$(unused)'];
  const child=spawnOwned(program,['-e',"process.stdin.setEncoding('utf8');let s='';process.stdin.on('data',v=>s+=v);process.stdin.on('end',()=>{console.log(JSON.stringify({args:process.argv.slice(1),input:s}));console.error('stderr 中文');process.exitCode=7;});",...tokens],{cwd:root,stdio:['pipe','pipe','pipe'],env:process.env},windows?tools:undefined);
  const done=closed(child);child.stdin!.end('input 中文\n');
  const result=await done;
  assert.equal(result.code,7,result.err);assert.deepEqual(JSON.parse(result.out),{args:tokens,input:'input 中文\n'});assert.match(result.err,/stderr 中文/);
 }finally{await rm(root,{recursive:true,force:true});}
});

if(process.platform==='win32') test('Windows process identity rejects a mismatched creation time',native,()=>{
 const identity=windowsProcessIdentity(tools,process.pid);assert.equal(identity.alive,true);
 assert.deepEqual(requireWindowsProcess(tools,identity),identity);
 assert.throws(()=>requireWindowsProcess(tools,{...identity,creationTime:'0'}),/identity changed/);
});

if(!windows)test('Linux cancellation still reaps a group after its leader exited',{timeout:15000},async()=>{
 const child=owned(['-e',"const{spawn}=require('node:child_process');const kid=spawn(process.execPath,['-e',\"process.on('SIGTERM',()=>{});console.log('READY '+process.pid);setInterval(()=>{},1000)\"],{stdio:['ignore','inherit','inherit']});kid.unref();setTimeout(()=>process.exit(0),100);"]);
 let text='';child.stdout!.on('data',b=>text+=b);const done=closed(child);
 try{
  await until(()=>child.exitCode===0&&text.includes('READY '));
  const descendant=Number(/READY (\d+)/.exec(text)![1]);process.kill(descendant,0);
  terminateOwned(child,'SIGTERM');terminateOwned(child,'SIGKILL');await done;
 }finally{
  try{process.kill(-child.pid!,'SIGKILL');}catch(e){assert.equal((e as NodeJS.ErrnoException).code,'ESRCH');}
 }
});

if(process.platform==='win32') test('forced supervisor termination reaps detached descendants and preserves an unrelated process',native,async()=>{
 const sentinel=spawn(process.execPath,['-e','setInterval(()=>{},1000)'],{stdio:'ignore'});
 const sentinelDone=closed(sentinel);
 let child:ChildProcess|undefined,done:ReturnType<typeof closed>|undefined;
 try{
  child=owned(['-e',"const{spawn}=require('node:child_process');const kid=spawn(process.execPath,['-e','setInterval(()=>{},1000)'],{detached:true,stdio:'ignore'});console.log(JSON.stringify([process.pid,kid.pid]));setInterval(()=>{},1000);"]);
  let text='';child.stdout!.on('data',b=>text+=b);done=closed(child);
  await until(()=>text.includes('\n'));const ids:number[]=JSON.parse(text.trim());assert.ok(ids.every(alive));
  const identity=windowsProcessIdentity(tools,child.pid!);assert.equal(identity.alive,true);
  terminateOwned(child,'SIGKILL');await done;await until(()=>ids.every(id=>!alive(id)));
  assert.equal(alive(sentinel.pid!),true,'an unrelated process must remain alive');
 }finally{if(child)terminateOwned(child,'SIGKILL');sentinel.kill('SIGKILL');await Promise.all([done,sentinelDone]);}
});

if(process.platform==='win32') test('normal leader exit also drains descendants before successful completion',native,async()=>{
 const child=owned(['-e',"const{spawn}=require('node:child_process');const kid=spawn(process.execPath,['-e','setInterval(()=>{},1000)'],{detached:true,stdio:'ignore'});kid.unref();console.log(kid.pid);"]);
 const result=await closed(child);assert.equal(result.code,0,result.err);
 assert.equal(alive(Number(result.out.trim())),false);
});

if(process.platform==='win32') test('parent death reclaims the helper and its Job without PID-based cancellation',native,async()=>{
 const root=await mkdtemp(path.join(tmpdir(),'ARCH parent-'));
 const helperModule=new URL('../host/hostProcess.ts',import.meta.url).href;
 const file=path.join(root,'parent.mjs');
 await writeFile(file,`import{spawnOwned}from ${JSON.stringify(helperModule)};const t=JSON.parse(process.argv[2]);const c=spawnOwned(process.execPath,['-e',"console.log(process.pid);setInterval(()=>{},1000)"],{stdio:['ignore','pipe','pipe']},t);console.log('HELPER '+c.pid);c.stdout.on('data',b=>process.stdout.write('TARGET '+b));setInterval(()=>{},1000);`);
 const parent=spawn(process.execPath,[file,JSON.stringify(tools)],{stdio:['ignore','pipe','pipe']});
 let text='';parent.stdout!.on('data',b=>text+=b);const done=closed(parent);
 try{
  await until(()=>text.includes('TARGET '));const ids=[Number(/HELPER (\d+)/.exec(text)![1]),Number(/TARGET (\d+)/.exec(text)![1])];
  assert.ok(ids.every(alive));parent.kill('SIGKILL');await done;await until(()=>ids.every(id=>!alive(id)));
 }finally{parent.kill('SIGKILL');await done;await rm(root,{recursive:true,force:true});}
});

if(process.platform==='win32') test('mismatched parent creation identity cannot execute the requested program',native,async()=>{
 const root=await mkdtemp(path.join(tmpdir(),'ARCH stale-parent-'));const marker=path.join(root,'must-not-exist');
 try{
  const child=spawn(tools.supervisor,['run',String(process.pid),'0','--',process.execPath,'-e',"require('node:fs').writeFileSync(process.argv[1],'bad')",marker],{stdio:['ignore','pipe','pipe']});
  const result=await closed(child);assert.equal(result.code,125);assert.match(result.err,/identity unavailable or changed/);
  await assert.rejects(access(marker));
 }finally{await rm(root,{recursive:true,force:true});}
});

if(process.platform==='win32') test('an independent worker outlives its Host and its own exit reclaims the Job',native,async()=>{
 const root=await mkdtemp(path.join(tmpdir(),'ARCH independent-'));
 const stateFile=path.join(root,'state.json'),stopFile=path.join(root,'stop');
 const worker=path.join(root,'worker.mjs'),host=path.join(root,'host.mjs');
 await writeFile(worker,`import{spawnOwned,terminateOwned}from ${JSON.stringify(new URL('../host/hostProcess.ts',import.meta.url).href)};import{writeFileSync,existsSync}from'node:fs';const t=JSON.parse(process.argv[2]),state=process.argv[3],stop=process.argv[4];const c=spawnOwned(process.execPath,['-e','console.log(process.pid);setInterval(()=>{},1000)'],{stdio:['ignore','pipe','pipe']},t);c.stdout.once('data',b=>writeFileSync(state,JSON.stringify([process.pid,c.pid,Number(String(b).trim())])));const deadline=Date.now()+20000;const timer=setInterval(()=>{if(existsSync(stop)||Date.now()>deadline)terminateOwned(c,'SIGKILL');},50);c.once('close',()=>{clearInterval(timer);process.exit(0);});`);
 await writeFile(host,`import{spawn}from'node:child_process';const c=spawn(process.execPath,process.argv.slice(2),{detached:true,stdio:'ignore'});c.unref();setInterval(()=>{},1000);`);
 const parent=spawn(process.execPath,[host,worker,JSON.stringify(tools),stateFile,stopFile],{stdio:'ignore'});const done=closed(parent);
 try{
  await until(async()=>access(stateFile).then(()=>true,()=>false));
  const ids:number[]=JSON.parse(await readFile(stateFile,'utf8'));assert.ok(ids.every(alive));
  parent.kill('SIGKILL');await done;await pause(300);assert.ok(ids.every(alive),'Host exit must not be worker exit');
  await writeFile(stopFile,'stop');await until(()=>ids.every(id=>!alive(id)));
 }finally{await writeFile(stopFile,'stop');parent.kill('SIGKILL');await done;await rm(root,{recursive:true,force:true});}
});

if(process.platform==='win32') test('native Configure cancellation retains the shared state machine and reaps its child',native,async()=>{
 const root=await mkdtemp(path.join(tmpdir(),'ARCH Configure 中文 space-'));
 await writeFile(path.join(root,'CMakeLists.txt'),'cmake_minimum_required(VERSION 3.20)\nproject(Cancel NONE)\nfile(WRITE "${CMAKE_BINARY_DIR}/entered" "yes")\nexecute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 30)\n');
 const runner=new ConfigureRunner({id:'native-cancel',sourceRoot:root,buildDirRelative:'build space',generator:'Ninja',definitions:{},windowsTools:tools});
 try{
  const work=runner.run('p','native-cancel');await assert.rejects(runner.run('p','native-cancel'),/already active/);
  await until(async()=>access(path.join(root,'build space/entered')).then(()=>true,()=>false));
  const helper=runner.processId;assert.ok(helper);await runner.shutdown();const result=await work;
  assert.equal(result.state,'cancelled');assert.equal(result.evidence,undefined);assert.equal(runner.isActive(),false);assert.equal(alive(helper),false);
  await assert.rejects(runner.run('p','native-cancel'),/shutting down/);
 }finally{await runner.shutdown();await rm(root,{recursive:true,force:true});}
});

if(process.platform==='win32') test('Windows tools are opt-in and a missing supervisor never falls back to an unowned child',native,()=>{
 assert.throws(()=>spawnOwned(process.execPath,[],{stdio:'ignore'}),/not configured/);
 assert.throws(()=>spawnOwned(process.execPath,[],{shell:true},tools),/Shell execution/);
});

// A1 permits host path spelling normalization while retaining binding/fingerprints.
if(process.platform==='win32') test('native Configure completes and repeats with A1 while rejecting foreign cache bindings',native,async()=>{
 const root=await mkdtemp(path.join(tmpdir(),'ARCH evidence 中文 space-'));
 await writeFile(path.join(root,'CMakeLists.txt'),'cmake_minimum_required(VERSION 3.20)\nproject(Observe NONE)\n');
 const runner=new ConfigureRunner({id:'native-evidence',sourceRoot:root,buildDirRelative:'build',generator:'Ninja',definitions:{},windowsTools:tools});
 try{
  const result=await runner.run('p','native-evidence');
  console.log('NATIVE_CONFIGURE_OBSERVATION '+JSON.stringify(result));
  assert.equal(result.exitCode,0,'the actual CMake command must have completed');
  assert.equal(result.state,'succeeded',result.error);assert.ok(result.evidence);
  assert.equal(result.evidence.dependenciesComplete,false);
  assert.ok(result.evidence.inputs.some(i=>i.path===path.join(root,'CMakeLists.txt')));
  assert.ok(result.evidence.inputs.every(i=>/^[a-f0-9]{64}$/.test(i.sha256)));
  assert.equal((await runner.run('p','native-evidence')).state,'succeeded');
  const cachePath=path.join(root,'build/CMakeCache.txt'),cache=await readFile(cachePath,'utf8');
  assert.match(cache,/CMAKE_GENERATOR:INTERNAL=Ninja/);
  for(const changed of [cache.replace(/CMAKE_HOME_DIRECTORY:INTERNAL=[^\r\n]*/,()=> 'CMAKE_HOME_DIRECTORY:INTERNAL='+root+'-other'),cache.replace('CMAKE_GENERATOR:INTERNAL=Ninja','CMAKE_GENERATOR:INTERNAL=Unix Makefiles'),cache+'\nCMAKE_HOME_DIRECTORY:INTERNAL='+root+'\n']){
   await writeFile(cachePath,changed);
   const rejected=await runner.run('p','native-evidence');
   assert.equal(rejected.state,'failed');assert.equal(rejected.exitCode,null);assert.match(rejected.error??'',/binding or generator differs/);assert.equal(rejected.evidence,undefined);
  }
  await writeFile(cachePath,cache);
 }finally{await runner.shutdown();await rm(root,{recursive:true,force:true});}
});
