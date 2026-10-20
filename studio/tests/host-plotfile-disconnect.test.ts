import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,copyFile,rm,readFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHostServer,listenLocal} from '../host/server.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
import type {ProjectSnapshot} from '../src/host/contracts.ts';

const delay=(ms:number)=>new Promise(resolve=>setTimeout(resolve,ms));
interface Worker {pid:number;start:string;state:string}
const workerPath=fileURLToPath(new URL('../host/plotfileMetadataWorker.ts',import.meta.url));
async function identity(pid:number):Promise<Worker|null>{
 try{
  const stat=await readFile('/proc/'+pid+'/stat','utf8'),fields=stat.slice(stat.lastIndexOf(')')+2).split(' ');
  const args=(await readFile('/proc/'+pid+'/cmdline','utf8')).split('\0');
  if(Number(fields[1])!==process.pid||!args.includes(workerPath))return null;
  return {pid,start:fields[19],state:fields[0]};
 }catch(error){if(['ENOENT','ESRCH'].includes((error as NodeJS.ErrnoException).code??''))return null;throw error;}
}
async function findOwnedWorker():Promise<Worker|null>{
 const children=(await readFile('/proc/'+process.pid+'/task/'+process.pid+'/children','utf8')).trim().split(/\s+/).filter(Boolean).map(Number);
 for(const pid of children){const worker=await identity(pid);if(worker)return worker;}
 return null;
}
test('disconnecting a real pending HTTP audit reaps its stalled owned worker and permits another read',async()=>{
 assert.equal(process.platform,'linux','This integration validation requires the authorized Linux/WSL lane.');
 const root=await mkdtemp(join(tmpdir(),'arch-http-plt-cancel-')),path=join(root,'result.h5');
 await copyFile(new URL('./fixtures/sod-1d.h5',import.meta.url),path);
 const before=await readFile(path);
 const snapshot:ProjectSnapshot={host:{protocolVersion:PROTOCOL_VERSION,hostKind:'local',platform:'linux',projectRoot:root,capabilities:{readProject:true,writeConfig:false,build:false,preview:false,watchFiles:false}},session:{projectId:'session',displayName:'project',projectRoot:root,sourceState:'unknown',configFileState:'unknown',binaryState:'unknown',mapping:'unknown',metadata:'unavailable',openedAt:new Date().toISOString(),refreshedAt:new Date().toISOString()}};
 const origin='http://127.0.0.1:5173',headers={Origin:origin,'X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION,'Content-Type':'application/json'};
 const server=createHostServer({snapshot:()=>snapshot,refresh:async()=>snapshot},origin);
 let owned:Worker|null=null;
 try{
  await listenLocal(server,0);const address=server.address();assert.ok(address&&typeof address!=='string');const base='http://127.0.0.1:'+address.port;
  const post=(route:string,body:unknown,signal?:AbortSignal)=>fetch(base+route,{method:'POST',headers,body:JSON.stringify(body),signal});
  const metadataResponse=await post('/api/plotfile/audit-metadata',{projectId:'session',relativePath:'result.h5'});
  assert.equal(metadataResponse.status,200);const metadata=await metadataResponse.json();
  for(const operation of ['metadata','slice']){
   const body={projectId:'session',relativePath:'result.h5',...(operation==='slice'?{expectedFileSha256:metadata.metadata.file.sha256,slice:{field:'DENS',block:0,start:[0],count:[8]}}:{})};
   const controller=new AbortController();let settled=false;
   const pending=post('/api/plotfile/audit-'+operation,body,controller.signal).then(response=>{settled=true;return {response};},error=>{settled=true;return {error};});
   const deadline=Date.now()+3000;
   while(!(owned=await findOwnedWorker())&&Date.now()<deadline)await delay(5);
   assert.ok(owned,'A live directly-owned fixed reader worker must exist.');
   assert.equal(settled,false);
   const check=await identity(owned.pid);assert.equal(check?.start,owned.start);
   process.kill(owned.pid,'SIGSTOP');
   const stoppedDeadline=Date.now()+500;
   let stopped=await identity(owned.pid);
   while(stopped?.state!=='T'&&Date.now()<stoppedDeadline){await delay(5);stopped=await identity(owned.pid);}
   assert.equal(stopped?.start,owned.start);assert.equal(stopped?.state,'T');
   await delay(100);assert.equal(settled,false,'Fault-injected worker must keep HTTP operation pending.');
   const started=performance.now();controller.abort();
   const outcome=await pending;assert.ok('error' in outcome&&outcome.error.name==='AbortError');
   const reapDeadline=Date.now()+3000;
   while(await identity(owned.pid)&&Date.now()<reapDeadline)await delay(5);
   assert.equal(await identity(owned.pid),null,'Cancelled worker must no longer exist under its recorded parent/start identity.');
   const reapedMs=performance.now()-started,pid=owned.pid,start=owned.start;owned=null;
   const recovered=await post('/api/plotfile/audit-metadata',{projectId:'session',relativePath:'result.h5'});
   assert.equal(recovered.status,200);assert.equal((await recovered.json()).metadata.file.sha256,metadata.metadata.file.sha256);
   console.log(JSON.stringify({scope:'live HTTP disconnect with explicit worker-stall injection',operation,pid,start,reapedMs,recoveryStatus:recovered.status}));
  }
  assert.deepEqual(await readFile(path),before);
 }finally{
  if(owned){const check=await identity(owned.pid);if(check?.start===owned.start)process.kill(owned.pid,'SIGKILL');}
  await new Promise<void>(resolve=>server.close(()=>resolve()));
  await rm(root,{recursive:true,force:true});
 }
});
