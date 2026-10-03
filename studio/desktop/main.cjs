const {app,BrowserWindow,ipcMain,dialog}=require('electron');
const {spawn,execFile}=require('node:child_process');
const {createServer,request:httpRequest}=require('node:http');
const {readFile,stat,mkdir,appendFile}=require('node:fs/promises');
const path=require('node:path');
const {randomBytes}=require('node:crypto');
let win,assets,origin,owned,ready,distro,linuxUser,dirty=false,closing=false;
const root=__dirname;
const linux=process.platform==='linux';
const contentRoot=app.isPackaged?root:path.dirname(root);
const singleInstance=app.requestSingleInstanceLock();
app.on('second-instance',()=>{if(win){if(win.isMinimized())win.restore();win.focus();void dialog.showMessageBox(win,{message:'ARCH Studio is already open.',detail:'Close the current project window before launching another project.'});}});
const log=async(message)=>{await mkdir(app.getPath('userData'),{recursive:true});await appendFile(path.join(app.getPath('userData'),'desktop.log'),new Date().toISOString()+' '+message+'\n');};
function exec(program,args){return new Promise((resolve,reject)=>execFile(program,args,{windowsHide:true,encoding:'utf8',timeout:15000,maxBuffer:1024*1024},(e,out,err)=>e?reject(new Error(err.trim()||e.message)):resolve(out.trim())));}
async function wsl(args){if(linux)return exec(args[0],args.slice(1));try{return await exec('wsl.exe',[...(distro?['-d',distro]:[]),...(linuxUser?['-u',linuxUser]:[]),'--exec',...args]);}catch(error){throw new Error('WSL environment '+(distro??'(default distribution)')+' could not complete this operation. Check WSL installation, selected distribution and project path, then retry. '+error.message,{cause:error});}}
async function mapPath(value){if(linux){if(typeof value!=='string'||value.includes('\0'))throw new Error('Invalid Linux path.');return path.resolve(value);}const {windowsAssociation}=await import('./arguments.mjs');const a=windowsAssociation(value,distro);if(a.distro)distro=a.distro;return a.linux??wsl(['/usr/bin/wslpath','-u',a.windows]);}
async function stopHost(){
 if(!owned)return;const child=owned;
 await log('shutdown requested WSL bridge PID '+child.pid);
 child.stdin.end();
 if(child.exitCode!==null||child.signalCode!==null){owned=null;ready=null;return;}
 await new Promise(resolve=>child.once('close',resolve));
 await log('owned Host exited; bridge PID '+child.pid);
 owned=null;ready=null;
}
async function launch(options){
 if(owned)throw new Error('An owned project is already open. Close this window before changing project.');
 const allowed=['project','binary','case','config','source','distro','cwd'];
 if(!options||Object.keys(options).some(k=>!allowed.includes(k))||Object.values(options).some(v=>typeof v!=='string'||v.includes('\0')))throw new Error('Invalid desktop launch options.');
 distro=options.distro||undefined;linuxUser=undefined;
 if(linux&&distro)throw new Error('Linux launcher does not select another WSL distribution.');
 const cwd=await mapPath(options.cwd||process.cwd());
 const project=options.project?await mapPath(options.project):undefined;
 const source=options.source ? (/^(?:[A-Za-z]:|\\\\|\/)/.test(options.source)?await mapPath(options.source):path.posix.resolve(cwd,options.source)) : undefined;
 const owner=await wsl(['/usr/bin/stat','-c','%U',project??source??cwd]);
 if(!/^[a-z_][a-z0-9_-]*[$]?$/.test(owner))throw new Error('Cannot resolve Linux project owner. Choose a Linux ARCH project.');linuxUser=owner;
 const values={cwd,project};
 for(const key of ['binary','config','source'])if(options[key])values[key]=/^(?:[A-Za-z]:|\\\\|\/)/.test(options[key])?await mapPath(options[key]):path.posix.resolve(project??cwd,options[key]);
 const home=await wsl(['/usr/bin/printenv','HOME']);
 const candidates=[home+'/.local/opt/node-studio/bin/node','/usr/bin/node'];let runtime;
 for(const candidate of candidates){try{const version=await wsl([candidate,'--version']);if(Number(version.match(/^v(\d+)/)?.[1])>=24){runtime=candidate;break;}}catch{/* next explicit prerequisite candidate */}}
 if(!runtime)throw new Error('Linux Node 24+ missing in the selected environment. Install a supported Node runtime, then reopen ARCH Studio.');
 const entry=await mapPath(path.join(contentRoot,'host','desktop.ts'));
 const token=randomBytes(32).toString('hex');
 const payload=Buffer.from(JSON.stringify({...values,caseId:options.case,origin,token})).toString('base64url');
 const child=spawn(linux?runtime:'wsl.exe',linux?[entry,payload]:[...(distro?['-d',distro]:[]),...(linuxUser?['-u',linuxUser]:[]),'--exec',runtime,entry,payload],{windowsHide:true,stdio:['pipe','pipe','pipe']});owned=child;
 let errors='';child.stderr.on('data',b=>{errors=(errors+b.toString()).slice(-16000);});child.stdin.on('error',()=>{});
 try{
  ready=await new Promise((resolve,reject)=>{
   let text='';const timer=setTimeout(()=>reject(new Error('Local Host startup timed out. '+errors)),25000);
   const fail=()=>{clearTimeout(timer);reject(new Error('Local Host failed: '+errors));};
   child.once('error',fail);child.once('close',fail);
   child.stdout.on('data',b=>{text+=b.toString();if(text.length>65536){clearTimeout(timer);reject(new Error('Host readiness output exceeded limit.'));return;}const end=text.indexOf('\n');if(end<0)return;try{const value=JSON.parse(text.slice(0,end));if(value.kind!=='desktop-ready'||value.token!==token||!Number.isInteger(value.port)||value.port<1||value.port>65535)throw new Error('Invalid Host readiness identity.');clearTimeout(timer);resolve(value);}catch(e){clearTimeout(timer);reject(e);}});
  });
  let connected=false,lastError='';
  for(let attempt=0;attempt<30;attempt++){
   try{const health=await fetch('http://127.0.0.1:'+ready.port+'/api/health',{headers:{Origin:origin,'X-ARCH-Studio':'1','X-ARCH-Protocol':'1.3','X-ARCH-Desktop-Token':token},signal:AbortSignal.timeout(1000)});
    if(!health.ok||(await health.json()).desktopToken!==token)throw new Error('Port belongs to an unverified service.');
    connected=true;break;
   }catch(e){lastError=e.message+' '+(e.cause?.code??'');await new Promise(r=>setTimeout(r,300));}
  }
  if(!connected)throw new Error('Owned Local Host connection unavailable: '+lastError+'. No unrelated service was adopted.');
  await log('ready '+JSON.stringify({bridgePid:child.pid,hostPid:ready.pid,port:ready.port,project:ready.project,caseId:ready.caseId,distro:distro??'default',linuxUser}));
  await createWindow({endpoint:origin,caseId:ready.caseId,project:ready.project},'/index.html');
 }catch(e){await stopHost();throw e;}
}
async function createWindow(bootstrap,page){
 const old=win;
 win=new BrowserWindow({width:1440,height:940,minWidth:1000,minHeight:640,title:'ARCH Studio',show:false,webPreferences:{preload:path.join(root,'preload.cjs'),contextIsolation:true,nodeIntegration:false,sandbox:true,additionalArguments:['--arch-bootstrap='+JSON.stringify({...bootstrap,platform:process.platform})]}});
 win.on('page-title-updated',event=>event.preventDefault());
 win.setTitle(bootstrap.project?'ARCH Studio — '+path.posix.basename(bootstrap.project):'ARCH Studio — Open project');
 win.webContents.setWindowOpenHandler(()=>({action:'deny'}));
 win.webContents.on('will-navigate',(event,url)=>{if(new URL(url).origin!==origin)event.preventDefault();});
 win.on('close',event=>{if(closing)return;event.preventDefault();void shutdown();});
 await win.loadURL(origin+page);win.setTitle(bootstrap.project?'ARCH Studio — '+path.posix.basename(bootstrap.project):'ARCH Studio — Open project');win.show();if(old)old.destroy();
}
async function shutdown(){
 if(closing)return;
 if(dirty){const choice=await dialog.showMessageBox(win,{type:'warning',buttons:['Keep editing','Discard unsaved copy and close'],defaultId:0,cancelId:0,message:'The Working Copy has unsaved changes.'});if(choice.response===0)return;}
 closing=true;win?.setTitle('ARCH Studio — shutting down Host (waiting for active Build if needed)');
 try{await stopHost();const {closeAssets}=await import('./close-assets.mjs');await closeAssets(assets);await log('desktop clean shutdown');win?.destroy();app.quit();}catch(e){closing=false;await dialog.showMessageBox(win,{type:'error',message:'Shutdown failed; window kept open.',detail:e.message});}
}
function senderAllowed(event){if(event.sender!==win?.webContents||new URL(event.senderFrame.url).origin!==origin)throw new Error('Unexpected desktop sender.');}
app.whenReady().then(async()=>{
 if(!singleInstance){closing=true;app.quit();return;}
 assets=createServer(async(req,res)=>{
  try{
   if(req.headers.host!==new URL(origin).host){res.writeHead(403).end();return;}
   const pathname=new URL(req.url,origin).pathname;
   if(pathname.startsWith('/api/')){
    if(!ready||req.headers['x-arch-studio']!=='1'||req.headers['x-arch-protocol']!=='1.3'||(req.headers.origin&&req.headers.origin!==origin)){res.writeHead(403).end();return;}
    const upstream=httpRequest({hostname:'127.0.0.1',port:ready.port,path:req.url,method:req.method,headers:{...req.headers,host:'127.0.0.1:'+ready.port,origin,'x-arch-desktop-token':ready.token}},r=>{res.writeHead(r.statusCode??502,r.headers);r.pipe(res);});
    upstream.on('error',()=>{if(!res.headersSent)res.writeHead(502);res.end('Owned Host unavailable');});req.pipe(upstream);return;
   }
   const native=['/launcher.html','/launcher.js','/launcher.css'];
   const base=native.includes(pathname)?root:path.join(contentRoot,'dist');
   const target=path.resolve(base,'.'+decodeURIComponent(pathname==='/'?'/index.html':pathname));
   if(!target.startsWith(base+path.sep)||!(await stat(target)).isFile()){res.writeHead(404).end();return;}
   const mime={'.html':'text/html','.js':'text/javascript','.css':'text/css','.wasm':'application/wasm','.json':'application/json','.svg':'image/svg+xml'};
   res.writeHead(200,{'Content-Type':mime[path.extname(target)]??'application/octet-stream','X-Content-Type-Options':'nosniff','Cache-Control':'no-store','Content-Security-Policy':"default-src 'self'; script-src 'self' 'unsafe-eval' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; worker-src 'self' blob:; connect-src 'self'; object-src 'none'; base-uri 'self'; frame-ancestors 'none'"});res.end(await readFile(target));
  }catch{res.writeHead(404).end();}
 });
 await new Promise((resolve,reject)=>{assets.once('error',reject);assets.listen(0,'127.0.0.1',resolve);});origin='http://127.0.0.1:'+assets.address().port;
 if(linux)ipcMain.handle('desktop:open-path',async event=>{
  senderAllowed(event);if(!ready)throw new Error('No managed project.');
  const d=await dialog.showOpenDialog(win,{title:'Open project configuration',defaultPath:ready.project,properties:['openFile'],filters:[{name:'ARCH configuration',extensions:['par']}]});
  if(d.canceled)return null;
  const selected=d.filePaths[0];if(typeof selected!=='string')throw new Error('No configuration selected.');
  const relative=path.relative(ready.project,path.resolve(selected));
  if(!relative||path.isAbsolute(relative)||relative.split(path.sep).includes('..')||!relative.endsWith('.par'))throw new Error('Choose a .par inside the managed project.');
  return relative;
 });
 ipcMain.handle('desktop:pick-project',async event=>{senderAllowed(event);const d=await dialog.showOpenDialog(win,{title:'Choose managed ARCH project',properties:['openDirectory']});return d.canceled?null:d.filePaths[0];});
 ipcMain.handle('desktop:launch',async(event,options)=>{senderAllowed(event);try{await launch(options);return {};}catch(e){return {error:e.message};}});
 ipcMain.handle('desktop:save-path',async(event,name)=>{senderAllowed(event);if(!ready||typeof name!=='string')throw new Error('No managed project.');const distribution=linux?'':distro||await exec('wsl.exe',['--exec','/usr/bin/printenv','WSL_DISTRO_NAME']);const rootWin=linux?ready.project:'\\\\wsl.localhost\\'+distribution+ready.project.replaceAll('/','\\');const d=await dialog.showSaveDialog(win,{title:'Save new project configuration',defaultPath:path.join(rootWin,path.basename(name)),filters:[{name:'ARCH configuration',extensions:['par']}]});if(d.canceled||!d.filePath)return null;const mapped=await mapPath(d.filePath);const relative=path.posix.relative(ready.project,mapped);if(relative.startsWith('../')||path.posix.isAbsolute(relative)||!relative.endsWith('.par'))throw new Error('Save As must select a new .par inside the managed project.');return relative;});
 ipcMain.on('desktop:dirty',(event,value)=>{senderAllowed(event);dirty=value===true;});
 const {parseLaunchArgs}=await import('./arguments.mjs');
 try{const entryIndex=process.argv.findIndex((value,index)=>index>0&&path.resolve(value)===__filename);
  const args=app.isPackaged?process.argv.slice(1):process.argv.slice(entryIndex>=1?entryIndex+1:2);await launch(parseLaunchArgs(args,process.cwd()));}
 catch(e){await log('launch failed: '+e.message);await createWindow({error:e.message},'/launcher.html');}
}).catch(async e=>{await log(e.message);app.quit();});
app.on('window-all-closed',()=>{if(!closing)void shutdown();});
app.on('before-quit',event=>{if(!closing){event.preventDefault();void shutdown();}});
