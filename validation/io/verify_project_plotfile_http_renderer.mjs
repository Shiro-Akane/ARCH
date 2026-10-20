#!/usr/bin/env node
/** Linux production ProjectPlotfileAudit + LocalHostProvider + actual HTTP Host. No simulation. */
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve,dirname,relative,join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import {createServer} from 'node:http';
import assert from 'node:assert/strict';
import {openProject} from '../../studio/host/project.ts';
import {createHostServer,listenLocal} from '../../studio/host/server.ts';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../..');
if(process.argv.length!==4)throw Error('Use LOCAL_H5PY_ORACLE_JSON NEW_LOCAL_OUTPUT_DIR');
const output=resolve(process.argv[3]);await mkdir(output);
const oracle=JSON.parse(await readFile(process.argv[2],'utf8'));
const sha=async p=>createHash('sha256').update(await readFile(p)).digest('hex');
for(const o of oracle){assert.equal(await sha(o.path),o.sha256);o.relativePath=relative(root,o.path);assert(!o.relativePath.startsWith('..'));}
const fixture=String.raw`import React from 'ROOT/studio/node_modules/react/index.js';
import {createRoot} from 'ROOT/studio/node_modules/react-dom/client.js';
import {LocalHostProvider} from 'ROOT/studio/src/host/LocalHostProvider.tsx';
import {ProjectPlotfileAudit} from 'ROOT/studio/src/components/ProjectPlotfileAudit.tsx';
import 'ROOT/studio/src/styles.css';
window.trace=[];window.byLabel=label=>Array.from(document.querySelectorAll("[aria-label]")).find(e=>e.getAttribute("aria-label")===label);
const original=window.fetch;
window.fetch=async(url,options)=>{
 const response=await original(url,options);
 const record={url:String(url),method:options?.method??'GET',request:options?.body?JSON.parse(options.body):null,
 status:response.status,response:await response.clone().json()};
 window.trace.push(record);return response;
};
createRoot(document.getElementById('root')).render(React.createElement(LocalHostProvider,null,React.createElement('main',{className:'sample-page plotfile-page'},React.createElement(ProjectPlotfileAudit))));
`;
const main=String.raw`const {app,BrowserWindow}=require('electron');
const fs=require('node:fs'),assert=require('node:assert/strict');
const output=__dirname,launch=JSON.parse(fs.readFileSync(output+'/launch.json')),records=[];let win;
const delay=ms=>new Promise(r=>setTimeout(r,ms)),js=s=>win.webContents.executeJavaScript(s);
async function waitFor(code){for(let i=0;i<150;i++){if(await js(code))return;await delay(100);}throw Error('Timed out: '+code);}
async function text(label,value){
 await js('(()=>{const e=window.byLabel('+JSON.stringify(label)+');e.focus();e.select();})()');
 win.webContents.insertText(String(value));await delay(100);
}
async function button(prefix){
 await js('Array.from(document.querySelectorAll(\"button\")).find(b=>b.textContent.startsWith('+JSON.stringify(prefix)+')).scrollIntoView({block:\"center\"})');await delay(150);
 const p=await js('(()=>{const e=Array.from(document.querySelectorAll("button")).find(b=>b.textContent.startsWith('+JSON.stringify(prefix)+'));if(!e||e.disabled)throw Error("Button unavailable");e.scrollIntoView({block:"center"});const b=e.getBoundingClientRect();return {x:b.x+b.width/2,y:b.y+b.height/2};})()');
 await input('mouseDown',p,{button:'left',clickCount:1});await input('mouseUp',p,{button:'left',clickCount:1});
}
async function input(type,p,extra={}){win.webContents.sendInputEvent({type,x:Math.round(p.x),y:Math.round(p.y),...extra});await delay(80);}
async function geometry(label,row){
 await js('window.byLabel('+JSON.stringify(label)+').scrollIntoView({block:\"center\"})');await delay(150);
 return js('(()=>{const svg=window.byLabel('+JSON.stringify(label)+');svg.scrollIntoView({block:"center"});const c=svg.querySelector("g[clip-path]").children['+row+'];const b=c.getBoundingClientRect();return {x:b.x+b.width/2,y:b.y+b.height/2,width:b.width};})()');
}
async function navigate(label,row){
 const requestsBefore=await js('window.trace.length');
 await geometry(label,row);await delay(150);
 const before=await geometry(label,row);
 await input('mouseMove',before);
 await input('mouseWheel',before,{deltaY:120,deltaX:0,canScroll:true});await delay(100);
 const zoom=await geometry(label,row);
 win.__debug={label,row,before,zoom,target:await js('(()=>{const e=document.elementFromPoint('+Math.round(before.x)+','+Math.round(before.y)+');return {tag:e?.tagName,text:e?.textContent?.slice(0,120),role:e?.getAttribute(\"aria-label\"),scrollY};})()')};
 assert.ok(Math.abs(zoom.width-before.width)>1,'wheel no-op');
 await input('mouseDown',zoom,{button:'left',clickCount:1});
 await input('mouseMove',{x:zoom.x+35,y:zoom.y+12},{movementX:35,movementY:12});
 await input('mouseUp',{x:zoom.x+35,y:zoom.y+12},{button:'left',clickCount:1});
 const panned=await geometry(label,row);win.__debug.panned=panned;assert.ok(Math.abs(panned.x-zoom.x-35)<1.1,'pan no-op');
 assert.equal(await js('window.trace.length'),requestsBefore,'display navigation fetched data');
 return panned;
}
app.whenReady().then(async()=>{try{
 win=new BrowserWindow({width:1280,height:900,show:true,webPreferences:{preload:output+'/preload.cjs',contextIsolation:true,nodeIntegration:false}});
 await win.loadURL(launch.origin);win.focus();
 await waitFor('!!window.byLabel("Project plotfile path")');
 for(const source of launch.oracle){
  await text('Project plotfile path',source.relativePath);await button('Read metadata');
  await waitFor('document.querySelector("[role=status]").textContent.includes("Metadata loaded")');
  await js('(()=>{const e=window.byLabel("Audit field");e.value="DENS";e.dispatchEvent(new Event("change",{bubbles:true}));})()');
  await text('Audit block',source.block);
  for(let axis=0;axis<source.dimension;axis++)await text('Audit count '+axis,source.count[axis]);
  await button('Read raw samples');
  await waitFor('!!window.byLabel("Native stored cell plot")');
  const at=await navigate('Native stored cell plot',source.row);
  await input('mouseDown',at,{button:'left',clickCount:1});await input('mouseUp',at,{button:'left',clickCount:1});
  await waitFor('window.byLabel("Plotfile sample Inspector").textContent.includes('+JSON.stringify(source.logicalKey)+')');
  const nativeText=await js('window.byLabel("Plotfile sample Inspector").textContent');
  assert.ok(nativeText.includes(String(source.value)));
  await button('Read global display LOD');
  await waitFor('!!window.byLabel("Global Plotfile LOD plot")');
  const pixel=source.dimension===1?16:12*32+16;
  const point=await navigate('Global Plotfile LOD plot',pixel);
  const beforePoints=await js('window.trace.filter(r=>r.url.endsWith("audit-point")).length');
  await input('mouseDown',point,{button:'left',clickCount:1});await input('mouseUp',point,{button:'left',clickCount:1});
  await waitFor('window.trace.filter(r=>r.url.endsWith("audit-point")).length>'+beforePoints);
  const actual=await js('window.trace.filter(r=>r.url.endsWith("audit-point")).at(-1)');
  assert.equal(actual.status,200);
  await waitFor('window.byLabel("Plotfile sample Inspector").textContent.includes("Queried physical point")');
  const inspector=await js('window.byLabel("Plotfile sample Inspector").textContent');
  assert.ok(inspector.includes(String(actual.response.result.payload.values[0])));
  const readsBefore=await js('window.trace.filter(r=>r.url.endsWith("audit-metadata")).length');
  await text('Project plotfile path',source.relativePath+'.missing');await button('Read metadata');
  await waitFor('window.trace.filter(r=>r.url.endsWith("audit-metadata")).length>'+readsBefore);
  const failed=await js('window.trace.filter(r=>r.url.endsWith("audit-metadata")).at(-1)');
  assert.equal(failed.status,400);
  assert.equal(await js('window.byLabel("Plotfile sample Inspector").textContent'),inspector);
  assert.ok(await js('!!window.byLabel("Global Plotfile LOD plot")'));
  records.push({caseId:source.producer.case_id,fileSha256:source.sha256,nativeText,pointTrace:actual,
   pointInspector:inspector,errorRetention:true,missingFileStatus:failed.status});
 }
 const trace=await js('window.trace');
 fs.writeFileSync(output+'/trace.json',JSON.stringify({records,trace},null,2)+'\n');
 win.destroy();app.quit();
}catch(e){fs.writeFileSync(output+'/debug.json',JSON.stringify({probe:win?.__debug,trace:win?await js('window.trace'):null},null,2));fs.writeFileSync(output+'/failure.txt',e.stack+'\n');console.error(e);win?.destroy();app.exit(1);}});
`;
await writeFile(output+'/fixture.jsx',fixture.replaceAll('ROOT',root));
await writeFile(output+'/index.html','<html><body style="margin:0;color:#ddd;background:#0f1921"><div id="root"></div><script type="module" src="./fixture.jsx"></script></body></html>');
const {build}=await import(root+'/studio/node_modules/vite/dist/node/index.js');
const {default:react}=await import(root+'/studio/node_modules/@vitejs/plugin-react/dist/index.js');
await build({configFile:false,root:output,base:'./',plugins:[react()],build:{outDir:output+'/dist',emptyOutDir:false},logLevel:'error'});
const assets=createServer(async(req,res)=>{try{
 const pathname=new URL(req.url,'http://127.0.0.1').pathname,local=pathname==='/'?'index.html':pathname.slice(1);
 assert(!local.includes('..'));
 const content=await readFile(join(output,'dist',local));
 res.setHeader('Content-Type',local.endsWith('.js')?'text/javascript':local.endsWith('.css')?'text/css':'text/html');res.end(content);
}catch{res.writeHead(404).end();}});
await listenLocal(assets,0);const origin='http://127.0.0.1:'+assets.address().port;
const project=await openProject({project:root});
const host=createHostServer(project,origin);await listenLocal(host,0);
const endpoint='http://127.0.0.1:'+host.address().port;
const requests=[];host.on('request',req=>requests.push({method:req.method,url:req.url}));
await writeFile(output+'/launch.json',JSON.stringify({origin,endpoint,oracle,projectId:project.snapshot().session.projectId}));
await writeFile(output+'/main.cjs',main);
await writeFile(output+'/preload.cjs',"const {contextBridge}=require('electron');contextBridge.exposeInMainWorld('archDesktop',{endpoint:"+JSON.stringify(endpoint)+"});");
try{
 const child=spawn(root+'/studio/node_modules/electron/dist/electron',['--no-sandbox','--disable-gpu',output+'/main.cjs'],{stdio:'inherit'});
 const timer=setTimeout(()=>child.kill('SIGTERM'),60000);
 const code=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',resolve);});clearTimeout(timer);
 assert.equal(code,0);
 const trace=JSON.parse(await readFile(output+'/trace.json','utf8'));assert.equal(trace.records.length,oracle.length);
 assert(requests.every(r=>['/api/project','/api/plotfile/audit-metadata','/api/plotfile/audit-slice','/api/plotfile/audit-overview','/api/plotfile/audit-point'].includes(r.url)));
 for(const o of oracle)assert.equal(await sha(o.path),o.sha256);
 await writeFile(output+'/transport-summary.json',JSON.stringify({requests,rawFilesUnchanged:true,origin,endpoint,scope:'Production components and HTTP Host; not full desktop launcher/human science UAT'},null,2));
 console.log('Production HTTP renderer trace saved: '+output);
}finally{
 await Promise.all([new Promise(r=>host.close(r)),new Promise(r=>assets.close(r))]);
}
