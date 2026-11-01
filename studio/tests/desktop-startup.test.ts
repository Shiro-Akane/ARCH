import test from 'node:test';
import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {stat} from 'node:fs/promises';
import Module,{createRequire} from 'node:module';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const repository=fileURLToPath(new URL('../../',import.meta.url));
const entry=fileURLToPath(new URL('../desktop/main.cjs',import.meta.url));
type Launch={token:string;project?:string;caseId?:string;config?:string;buildDir?:string;binary?:string;source?:string};
type WindowRecord={url?:string;bootstrap:Record<string,unknown>;visible:boolean};

/** Exercise the real Electron main entry while keeping windows and Host processes local to the test. */
async function startDesktop(args:string[],packaged=false){
 const loader=Module as unknown as {_load:(request:string,parent:unknown,isMain:boolean)=>unknown};
 const require=createRequire(import.meta.url),previousModule=require.cache[entry];
 const originalLoad=loader._load,originalArgv=process.argv,originalCwd=process.cwd(),originalFetch=globalThis.fetch;
 const originalNode=process.env.ARCH_STUDIO_NODE,originalTools=process.env.ARCH_STUDIO_WINDOWS_TOOLS;
 const windows:WindowRecord[]=[],launches:Launch[]=[],probes:string[]=[],reads:string[]=[],logs:string[]=[];
 let startup:Promise<void>|undefined;
 const app={isPackaged:packaged,requestSingleInstanceLock:()=>true,on:()=>{},
  whenReady:()=>({then:(callback:()=>Promise<void>)=>{startup=Promise.resolve().then(callback);return startup;}}),
  getPath:()=>repository,quit:()=>{throw new Error('Unexpected desktop quit.');}};
 class BrowserWindow {
  record:WindowRecord;
  webContents={setWindowOpenHandler:()=>{},on:()=>{}};
  constructor(options:{webPreferences:{additionalArguments:string[]}}){
   const value=options.webPreferences.additionalArguments.find(value=>value.startsWith('--arch-bootstrap='));
   assert.ok(value);this.record={bootstrap:JSON.parse(value.slice('--arch-bootstrap='.length)),visible:false};windows.push(this.record);
  }
  on(){} setTitle(){} isDestroyed(){return false;} destroy(){}
  async loadURL(url:string){this.record.url=url;}
  show(){this.record.visible=true;} isVisible(){return this.record.visible;}
 }
 const server=Object.assign(new EventEmitter(),{listen:(_port:number,_host:string,callback:()=>void)=>queueMicrotask(callback),address:()=>({port:49152})});
 const childProcesses={
  execFile:(program:string,argv:string[],_options:unknown,callback:(error:null,out:string,err:string)=>void)=>{
   probes.push(program);
   const out=program==='/usr/bin/stat'?'testuser':program==='/usr/bin/printenv'?repository:
    argv[0]==='--version'?process.version:JSON.stringify({node:process.versions.node,platform:process.platform,arch:process.arch});
   queueMicrotask(()=>callback(null,out,''));
  },
  spawn:(_program:string,argv:string[],options:{shell:boolean})=>{
   assert.equal(options.shell,false);
   const launch:Launch=JSON.parse(Buffer.from(argv[1],'base64url').toString('utf8'));launches.push(launch);
   const child=Object.assign(new EventEmitter(),{pid:12345,exitCode:null,signalCode:null,
    stdin:Object.assign(new EventEmitter(),{end:()=>{}}),stdout:new EventEmitter(),stderr:new EventEmitter()});
   queueMicrotask(()=>child.stdout.emit('data',Buffer.from(JSON.stringify({kind:'desktop-ready',token:launch.token,port:49153,pid:12346,project:launch.project,caseId:launch.caseId})+'\n')));
   return child;
  },
 };
 loader._load=function(request,parent,isMain){
  if(request==='electron')return {app,BrowserWindow,ipcMain:{handle:()=>{},on:()=>{}},dialog:{}};
  if(request==='node:child_process')return childProcesses;
  if(request==='node:http')return {createServer:()=>server};
  if(request==='node:fs/promises')return {
   mkdir:async()=>{},appendFile:async(_file:string,text:string)=>{logs.push(text);},
   readFile:async(file:string)=>{reads.push(file);throw new Error('Unexpected startup file read.');},stat,
  };
  return originalLoad.call(this,request,parent,isMain);
 };
 try{
  process.chdir(repository);
  process.argv=packaged?[process.execPath,...args]:[process.execPath,entry,...args];
  process.env.ARCH_STUDIO_NODE=process.execPath;
  process.env.ARCH_STUDIO_WINDOWS_TOOLS=JSON.stringify({cmake:process.execPath,supervisor:process.execPath,
   ninja:process.execPath,objdump:process.execPath,publisher:process.execPath,rc:process.execPath,path:path.dirname(process.execPath)});
  globalThis.fetch=async()=>new Response(JSON.stringify({desktopToken:launches.at(-1)?.token}),{status:200});
  delete require.cache[entry];require(entry);assert.ok(startup);await startup;
  return {windows,launches,probes,reads,logs};
 }finally{
  loader._load=originalLoad;process.argv=originalArgv;process.chdir(originalCwd);globalThis.fetch=originalFetch;
  if(originalNode===undefined)delete process.env.ARCH_STUDIO_NODE;else process.env.ARCH_STUDIO_NODE=originalNode;
  if(originalTools===undefined)delete process.env.ARCH_STUDIO_WINDOWS_TOOLS;else process.env.ARCH_STUDIO_WINDOWS_TOOLS=originalTools;
  delete require.cache[entry];if(previousModule)require.cache[entry]=previousModule;
 }
}

for(const packaged of [false,true])test(`${packaged?'packaged':'source'} no-argument startup opens an empty chooser even inside an ARCH project`,async()=>{
 assert.ok((await stat(path.join(repository,'CMakeLists.txt'))).isFile());
 assert.ok((await stat(path.join(repository,'simulation/Sod/Sod.par'))).isFile());
 const result=await startDesktop([],packaged);
 assert.equal(result.windows.length,1);const window=result.windows[0];
 assert.equal(new URL(window.url!).pathname,'/launcher.html');assert.equal(window.visible,true);
 for(const key of ['project','caseId','config','selectedSource','endpoint','error'])assert.equal(window.bootstrap[key],undefined,key);
 assert.deepEqual(result.launches,[]);assert.deepEqual(result.probes,[]);assert.deepEqual(result.reads,[]);
 assert.ok(!result.logs.some(line=>line.includes('launch failed')));
});

test('explicit project and model arguments still start Host and open the workbench',async()=>{
 const result=await startDesktop(['--project',repository,'--build-dir','build-selected','--binary','build-selected/bin/ARCH',
  '--case','Sedov','--source','simulation/Sedov/Sedov.cpp','--config','simulation/Sedov/Sedov.par']);
 assert.equal(result.launches.length,1);const launch=result.launches[0];
 assert.equal(launch.project,path.resolve(repository));assert.equal(launch.caseId,'Sedov');
 assert.equal(launch.config,path.resolve(repository,'simulation/Sedov/Sedov.par'));
 assert.equal(launch.source,path.resolve(repository,'simulation/Sedov/Sedov.cpp'));
 assert.equal(launch.buildDir,path.resolve(repository,'build-selected'));
 assert.equal(launch.binary,path.resolve(repository,'build-selected/bin/ARCH'));
 assert.equal(result.windows.length,1);assert.equal(new URL(result.windows[0].url!).pathname,'/index.html');
 assert.equal(result.windows[0].bootstrap.caseId,'Sedov');assert.equal(result.windows[0].bootstrap.error,undefined);
});
