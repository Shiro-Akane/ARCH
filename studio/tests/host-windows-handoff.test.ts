import test from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtemp,mkdir,copyFile,writeFile,readFile,rm} from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {RunController} from '../host/runController.ts';
import type {RunPreparationRunner} from '../host/runPreparation.ts';
import {readConfig} from '../host/config.ts';
import {fingerprint} from '../host/files.ts';
import {canonicalOutputDirectory} from '../host/runOutput.ts';
import type {WindowsRunTools} from '../host/windowsRun.ts';
import type {RunState} from '../src/host/runContracts.ts';

const native=process.platform==='win32';
async function until<T>(read:()=>Promise<T>,accept:(value:T)=>boolean,timeout=15000):Promise<T>{
 const end=Date.now()+timeout;let latest:T;
 while(Date.now()<end){try{latest=await read();if(accept(latest))return latest;}catch(error){if(![409,'ENOENT'].includes((error as {status?:number;code?:string}).status??(error as {code?:string}).code??''))throw error;}await new Promise(resolve=>setTimeout(resolve,50));}
 throw new Error('Native handoff did not reach its required observed state.');
}
test('native terminal worker survives Host exit, excludes a second run and confirms Stop cleanup',{skip:!native,timeout:45000},async t=>{
 for(const name of ['ARCH_TEST_SUPERVISOR','ARCH_TEST_TERMINAL'])assert.ok(process.env[name],name+' is required');
 const tools:WindowsRunTools={supervisor:process.env.ARCH_TEST_SUPERVISOR!,terminal:process.env.ARCH_TEST_TERMINAL!,path:process.env.PATH!};
 const root=await mkdtemp(path.join(os.tmpdir(),'arch-handoff 中文 '));await mkdir(path.join(root,'studio'));
 const text='# controlled Node fixture; no simulation or science qualification\n';
 await writeFile(path.join(root,'case.par'),text);await copyFile(process.execPath,path.join(root,'native.exe'));
 await writeFile(path.join(root,'Hold'),"const fs=require('node:fs');fs.writeFileSync(process.argv[2]+'.started',String(process.pid));setInterval(()=>{},1000);\n");
 const config=await readConfig(root,'case.par','p'),binary=await fingerprint(root,'native.exe','executable');
 assert.ok(binary.sha256&&binary.modifiedTime&&binary.size);
 const output=path.join(root,'output'),directories=[{path:output,canonicalPath:await canonicalOutputDirectory(output)}];
 const confirmed={plan:{caseId:'Hold',mode:'run',binary:{relativePath:'native.exe',fingerprint:{sha256:binary.sha256,size:binary.size,modifiedTime:binary.modifiedTime}},config:{relativePath:'case.par',fingerprint:config.fingerprint}},configText:text,outputDirectories:directories};
 const preparing={root,projectId:'p',consume:async()=>confirmed} as unknown as RunPreparationRunner;
 const controller=new RunController(preparing,{windowsTools:tools});
 const acceptedFile=path.join(root,'accepted.json');
 const launcher=path.join(root,'host.mjs');
 await writeFile(launcher,`import {RunController} from ${JSON.stringify(new URL('../host/runController.ts',import.meta.url).href)};\nimport {writeFile} from 'node:fs/promises';\nconst c=new RunController({root:${JSON.stringify(root)},projectId:'p',consume:async()=>(${JSON.stringify(confirmed)})},{windowsTools:${JSON.stringify(tools)}});\nconst accepted=await c.start({projectId:'p',planId:'fixture',confirmation:'run-saved-input-with-compiled-binary'});\nawait writeFile(${JSON.stringify(acceptedFile)},JSON.stringify(accepted));process.exit(0);\n`);
 const ids:string[]=[];
 t.after(async()=>{
  for(const id of ids){await writeFile(path.join(root,'studio/.local/runs',id,'stop-request'),id).catch(()=>undefined);}
  let safe=true;
  for(const id of ids){try{await until(()=>controller.status(id),s=>!['starting','running'].includes(s.state),12000);}catch{safe=false;}}
  if(safe)await rm(root,{recursive:true,force:true});
  assert.ok(safe,'owned fixture remained active; preserve its directory for identity-safe cleanup');
 });
 const host=spawn(process.execPath,[launcher],{shell:false,stdio:['ignore','pipe','pipe']});
 let errors='';host.stderr.on('data',chunk=>{errors+=String(chunk);});
 const hostClosed=new Promise<number|null>((resolve,reject)=>{host.once('error',reject);host.once('close',resolve);});
 const accepted=await until(()=>readFile(acceptedFile,'utf8').then(JSON.parse),x=>typeof x.runId==='string');ids.push(accepted.runId);
 assert.equal(await hostClosed,0,errors);
 const first=await until(()=>controller.status(accepted.runId),s=>s.state==='running');
 assert.ok(first.windowsWorker&&first.windowsHelper);
 await until(()=>readFile(path.join(root,'studio/.local/runs',accepted.runId,'input.par.started'),'utf8'),x=>/^\d+$/.test(x));
 await new Promise(resolve=>setTimeout(resolve,200));assert.equal((await controller.status(accepted.runId)).state,'running');
 const second=await controller.start({projectId:'p',planId:'fixture',confirmation:'run-saved-input-with-compiled-binary'});ids.push(second.runId);
 const rejected=await until(()=>controller.status(second.runId),s=>!['starting','running'].includes(s.state));assert.equal(rejected.state,'failed');
 await controller.stop(accepted.runId);
 const stopped:RunState=await until(()=>controller.status(accepted.runId),s=>!['starting','running'].includes(s.state));
 assert.equal(stopped.state,'stopped');assert.equal(stopped.exitCode,130,'helper must acknowledge Job drain before Stop succeeds');
 const third=await controller.start({projectId:'p',planId:'fixture',confirmation:'run-saved-input-with-compiled-binary'});ids.push(third.runId);
 await until(()=>controller.status(third.runId),s=>s.state==='running');await controller.stop(third.runId);
 assert.equal((await until(()=>controller.status(third.runId),s=>!['starting','running'].includes(s.state))).state,'stopped');
});
