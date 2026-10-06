#!/usr/bin/env node
/** Real Linux Electron input events against production components, using synthetic engineering geometry. */
import {mkdir,writeFile} from 'node:fs/promises';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../..');
if(!process.argv[2])throw Error('Usage: node verify_plotfile_renderer_events.mjs NEW_LOCAL_OUTPUT_DIR');
const output=resolve(process.argv[2]);await mkdir(output);
const {build}=await import(root+'/studio/node_modules/vite/dist/node/index.js');
const {default:react}=await import(root+'/studio/node_modules/@vitejs/plugin-react/dist/index.js');
const fixture=`import React from 'ROOT/studio/node_modules/react/index.js';
import {createRoot} from 'ROOT/studio/node_modules/react-dom/client.js';
import {PlotfileNativeView} from 'ROOT/studio/src/components/PlotfileNativeView.tsx';
import {PlotfileOverviewView} from 'ROOT/studio/src/components/PlotfileOverviewView.tsx';
const root=createRoot(document.getElementById('root'));
let source;
window.calls=[];window.network=[];
window.fetch=(...args)=>{window.network.push(args);throw Error('Unexpected display fetch');};
window.mount=(mode,d)=>{
 const n=d===1?3:6,low={x1:[0,1,2,0,1,2],x2:[10,10,10,11,11,11],x3:Array(6).fill(0)};
 const hi={x1:[1,2,3,1,2,3],x2:[11,11,11,12,12,12],x3:Array(6).fill(0)};
 const coords=a=>Object.fromEntries(Object.entries(a).map(([k,v])=>[k,v.slice(0,n).map(x=>d===1&&k==='x2'?0:x)]));
 const native={lower:coords(low),upper:coords(hi),level:1,logicalKey:'1/0/0/0'};
 source={projectId:'synthetic',relativePath:'engineering-fixture',audit:{
 file:{sha256:'a'.repeat(64)},time:0,dimension:d,geometry:'cartesian',fields:[{name:'DENS',unit:null}],coordinates:{units:null},
 payload:{field:'DENS',block:0,start:[0],shape:d===1?[3]:[2,3],linearIndices:Array.from({length:n},(_,i)=>i),
 values:[1,.125,.125,4,5,6].slice(0,n),nativeCells:native},
 overview:{field:'DENS',width:3,height:d===1?1:2,dimension:d,domain:{x:[0,3],y:d===1?[0,1]:[10,12]},
 values:[1,.125,.125,4,5,6].slice(0,n),representativeIndices:Array.from({length:n},(_,i)=>i),scannedCells:n,diagnostics:[]}}};
 window.calls=[];window.rawBefore=JSON.stringify(source);
 root.render(React.createElement(mode==='native'?PlotfileNativeView:PlotfileOverviewView,{
 key:mode+d,samples:source,selectedRow:null,onSelect:row=>window.calls.push({row}),disabled:false,
 onInspect:row=>window.calls.push({row}),onPoint:point=>window.calls.push({point})}));
};
window.snapshot=()=>({calls:window.calls,network:window.network,rawUnchanged:JSON.stringify(source)===window.rawBefore});
`;
const main=`const {app,BrowserWindow}=require('electron');
const fs=require('node:fs'),assert=require('node:assert/strict');
const output=__dirname,records=[];let win;
const delay=ms=>new Promise(r=>setTimeout(r,ms));
const js=s=>win.webContents.executeJavaScript(s);
async function geometry(row){return js('(()=>{const c=document.querySelector("svg g[clip-path]").children['+row+'];const b=c.getBoundingClientRect();return {x:b.x+b.width/2,y:b.y+b.height/2,width:b.width};})()');}
async function input(type,p,extra={}){win.webContents.sendInputEvent({type,x:Math.round(p.x),y:Math.round(p.y),...extra});await delay(80);}
app.whenReady().then(async()=>{try{
 win=new BrowserWindow({width:1280,height:900,show:true,webPreferences:{contextIsolation:true,nodeIntegration:false}});
 await win.loadFile(output+'/dist/index.html');win.focus();
 for(const size of [[1280,900],[1920,1080]])for(const mode of ['native','overview'])for(const d of [1,2]){
  win.setSize(...size);await delay(100);
  await js('window.mount('+JSON.stringify(mode)+','+d+')');await delay(150);
  const row=d===1?1:4,expected=d===1?[1.5]:[1.5,11.5],before=await geometry(row);
  await input('mouseWheel',before,{deltaY:120,deltaX:0,canScroll:true});await delay(100);
  const zoom=await geometry(row);
  assert.ok(Math.abs(zoom.width-before.width)>1,'wheel did not change geometry');
  await input('mouseDown',zoom,{button:'left',clickCount:1});
  await input('mouseMove',{x:zoom.x+45,y:zoom.y+18},{movementX:45,movementY:18});
  await input('mouseUp',{x:zoom.x+45,y:zoom.y+18},{button:'left',clickCount:1});
  assert.equal((await js('window.snapshot()')).calls.length,0,'drag selected a cell');
  const at=await geometry(row);
  assert.ok(Math.abs(at.x-zoom.x-45)<1.1,'pan did not move geometry');
  assert.ok(Math.abs(at.y-zoom.y-(d===1?0:18))<1.1,'pan ordinate');
  await input('mouseDown',at,{button:'left',clickCount:1});
  await input('mouseUp',at,{button:'left',clickCount:1});
  const state=await js('window.snapshot()');
  assert.equal(state.calls.length,1);
  if(mode==='native')assert.equal(state.calls[0].row,row);
  else expected.forEach((v,i)=>assert.ok(Math.abs(state.calls[0].point[i]-v)<.015,'physical mapping'));
  assert.equal(state.rawUnchanged,true);assert.equal(state.network.length,0);
  await js('Array.from(document.querySelectorAll("button")).find(b=>b.textContent.startsWith("Fit")).click()');await delay(100);
  const fit=await geometry(row);
  assert.ok(Math.abs(fit.x-before.x)<.01&&Math.abs(fit.y-before.y)<.01,'Fit geometry');
  records.push({windowSize:size,mode,dimension:d,shape:d===1?[3]:[2,3],status:'PASS',selection:state.calls[0],
   rawUnchanged:true,networkCalls:0,dragSelectCalls:0,fitRestored:true});
 }
 fs.writeFileSync(output+'/summary.json',JSON.stringify({status:'PASS',scope:'Linux Electron Chromium events; synthetic geometry; not scientific or human UAT',
 electron:process.versions.electron,chromium:process.versions.chrome,records},null,2)+'\n');
 win.destroy();app.quit();
}catch(e){fs.writeFileSync(output+'/failure.txt',e.stack+'\n');console.error(e);win?.destroy();app.exit(1);}});
`;
await writeFile(output+'/fixture.jsx',fixture.replaceAll('ROOT',root));
await writeFile(output+'/index.html','<html><body style="margin:0"><div id="root"></div><script type="module" src="./fixture.jsx"></script></body></html>');
await build({configFile:false,root:output,base:'./',plugins:[react()],build:{outDir:output+'/dist',emptyOutDir:false},logLevel:'error'});
await writeFile(output+'/main.cjs',main.replaceAll('ROOT',root));
const child=spawn(root+'/studio/node_modules/electron/dist/electron',['--no-sandbox','--disable-gpu',output+'/main.cjs',output],{stdio:'inherit'});
const timer=setTimeout(()=>child.kill('SIGTERM'),30000);
const code=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',resolve);});
clearTimeout(timer);
if(code!==0)throw Error('Renderer event verification failed; evidence retained at '+output);
console.log('PASS: '+output+'/summary.json');
