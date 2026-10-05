#!/usr/bin/env node
/** Existing HDF5 -> actual production components -> isolated reader -> actual Inspector. */
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import assert from 'node:assert/strict';
import {readPlotfileFieldSliceIsolated} from '../../studio/host/isolatedPlotfileMetadata.ts';
import {validatePlotfileAudit} from '../../studio/src/host/plotfileAudit.ts';
import {PROTOCOL_VERSION} from '../../studio/src/host/contracts.ts';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../..');
if(process.argv.length!==4)throw Error('Use LOCAL_H5PY_ORACLE_JSON NEW_LOCAL_OUTPUT_DIR');
const output=resolve(process.argv[3]);await mkdir(output);
const oracle=JSON.parse(await readFile(process.argv[2],'utf8')),inputs=[];
const sha=async p=>createHash('sha256').update(await readFile(p)).digest('hex');
for(const o of oracle){
 assert.equal(await sha(o.path),o.sha256);
 const selection={field:o.field,block:o.block,start:o.start,count:o.count};
 const result=await readPlotfileFieldSliceIsolated(o.path,selection);
 const samples=validatePlotfileAudit({protocolVersion:PROTOCOL_VERSION,projectId:'renderer-readback',relativePath:o.path,result},
  'renderer-readback',o.path,selection,o.sha256);
 inputs.push({id:inputs.length,oracle:o,samples});
}
await writeFile(output+'/inputs.json',JSON.stringify(inputs));
const fixture=String.raw`import React,{useState} from 'ROOT/studio/node_modules/react/index.js';
import {createRoot} from 'ROOT/studio/node_modules/react-dom/client.js';
import {PlotfileNativeView} from 'ROOT/studio/src/components/PlotfileNativeView.tsx';
import {PlotfileNativeInspector} from 'ROOT/studio/src/components/PlotfileNativeInspector.tsx';
import inputs from './inputs.json';
const root=createRoot(document.getElementById('root'));
window.calls=[];window.network=[];
window.fetch=(...args)=>{window.network.push(args);throw Error('Unexpected network');};
function View({input}){
 const [selected,setSelected]=useState(null),[point,setPoint]=useState(null);
 async function select(row){
  const p=input.samples.audit.payload;
  const coordinates=[p.coordinates.x[row],...(input.oracle.dimension===2?[p.coordinates.y[row]]:[])];
  const response=await window.probe.point({input:input.id,coordinates});
  window.calls.push({row,response});setSelected(row);setPoint(response);
 }
 return React.createElement(React.Fragment,null,
  React.createElement(PlotfileNativeView,{samples:input.samples,selectedRow:selected,onSelect:select}),
  point&&React.createElement(PlotfileNativeInspector,{samples:point,row:0}));
}
window.mount=id=>{window.calls=[];window.current=inputs[id];window.rawBefore=JSON.stringify(inputs);
 root.render(React.createElement(View,{key:id,input:inputs[id]}));};
window.snapshot=()=>({calls:window.calls,network:window.network,rawUnchanged:JSON.stringify(inputs)===window.rawBefore,
 inspector:document.querySelector('[aria-label="Plotfile sample Inspector"]')?.textContent??null});
`;
const main=String.raw`const {app,BrowserWindow,ipcMain}=require('electron');
const fs=require('node:fs'),assert=require('node:assert/strict'),{execFile}=require('node:child_process');
const {promisify}=require('node:util');
const output=__dirname,inputs=JSON.parse(fs.readFileSync(output+'/inputs.json')),records=[];let win;
const delay=ms=>new Promise(r=>setTimeout(r,ms)),js=s=>win.webContents.executeJavaScript(s);
ipcMain.handle('probe-point',async(event,query)=>{
 assert.equal(event.sender,win.webContents);
 assert(Number.isSafeInteger(query.input)&&query.input>=0&&query.input<inputs.length);
 const row=inputs[query.input],point=query.coordinates;
 assert(Array.isArray(point)&&point.length===row.oracle.dimension&&point.every(Number.isFinite));
 const {stdout}=await promisify(execFile)('NODE',[output+'/bridge.mjs',row.oracle.path,JSON.stringify(point)],
  {timeout:16000,maxBuffer:128*1024});
 return JSON.parse(stdout);
});
async function geometry(row){return js('(()=>{const c=document.querySelector("svg g[clip-path]").children['+row+'];const b=c.getBoundingClientRect(),m=document.querySelector("svg").getScreenCTM().inverse(),q=new DOMPoint(b.x+b.width/2,b.y+b.height/2).matrixTransform(m);return {x:b.x+b.width/2,y:b.y+b.height/2,width:b.width,sx:q.x,sy:q.y};})()');}
async function input(type,p,extra={}){win.webContents.sendInputEvent({type,x:Math.round(p.x),y:Math.round(p.y),...extra});await delay(80);}
const bits=v=>{const b=Buffer.alloc(8);b.writeDoubleLE(v);return b.toString('hex');};
app.whenReady().then(async()=>{try{
 win=new BrowserWindow({width:1280,height:900,show:true,webPreferences:{preload:output+'/preload.cjs',contextIsolation:true,nodeIntegration:false}});
 await win.loadFile(output+'/dist/index.html');win.focus();
 for(const source of inputs){
  await js('window.mount('+source.id+')');await delay(150);
  const o=source.oracle,before=await geometry(o.row);
  await input('mouseWheel',before,{deltaY:120,deltaX:0,canScroll:true});await delay(100);
  const zoom=await geometry(o.row);
  assert.ok(Math.abs(zoom.width-before.width)>1,'wheel no-op');
  await input('mouseDown',zoom,{button:'left',clickCount:1});
  await input('mouseMove',{x:zoom.x+45,y:zoom.y+18},{movementX:45,movementY:18});
  await input('mouseUp',{x:zoom.x+45,y:zoom.y+18},{button:'left',clickCount:1});
  const at=await geometry(o.row);assert.ok(Math.abs(at.x-zoom.x-45)<1.1,'pan no-op');
  assert.equal((await js('window.snapshot()')).calls.length,0);
  await input('mouseDown',at,{button:'left',clickCount:1});await input('mouseUp',at,{button:'left',clickCount:1});
  let state;
  for(let attempt=0;attempt<100;attempt++){state=await js('window.snapshot()');if(state.calls.length&&state.inspector)break;await delay(100);}
  assert.equal(state.calls.length,1);assert.equal(state.calls[0].row,o.row);
  const a=state.calls[0].response.audit,p=a.payload,n=p.nativeCells;
  assert.equal(a.file.sha256,o.sha256);assert.equal(p.linearIndices[0],o.index);
  assert.equal(bits(p.values[0]),o.valueBits);assert.equal(bits(n.cellMeasure[0]),o.measureBits);
  assert.equal(n.logicalKey,o.logicalKey);assert.equal(n.level,o.level);
  for(const axis of ['x1','x2','x3']){assert.equal(n.lower[axis][0],o.lower[axis]);assert.equal(n.upper[axis][0],o.upper[axis]);}
  assert.ok(state.inspector.includes(String(o.value)));
  assert.ok(state.inspector.includes(o.sha256)&&state.inspector.includes(o.logicalKey));
  assert.equal(state.rawUnchanged,true);assert.equal(state.network.length,0);
  await js('Array.from(document.querySelectorAll("button")).find(b=>b.textContent.startsWith("Fit")).click()');await delay(100);
  const fit=await geometry(o.row);assert.ok(Math.abs(fit.sx-before.sx)<.01&&Math.abs(fit.sy-before.sy)<.01);
  records.push({caseId:o.producer.case_id,fileSha256:o.sha256,producer:o.producer,dimension:o.dimension,
   shape:o.count,block:o.block,index:o.index,level:o.level,logicalKey:o.logicalKey,
   status:'PASS',rawValueBits:o.valueBits,cellMeasureBits:o.measureBits,
   rawValueBoundsMeasure:'h5py bit/number exact',inspectorText:'PASS',rawResponseUnchanged:true,
   wheelGeometryChanged:true,panGeometryChanged:true,fitRestored:true,dragSelectionCalls:0});
 }
 fs.writeFileSync(output+'/summary.json',JSON.stringify({status:'PASS',
 scope:'Actual Linux renderer click -> stored-row center -> production isolated point reader/client validator -> actual Inspector; existing H5; not HTTP/Host or human/scientific UAT',
 electron:process.versions.electron,chromium:process.versions.chrome,records},null,2)+'\n');
 win.destroy();app.quit();
}catch(e){fs.writeFileSync(output+'/failure.txt',e.stack+'\n');console.error(e);win?.destroy();app.exit(1);}});
`;
const bridge=String.raw`import {readPlotfilePointIsolated} from 'ROOT/studio/host/isolatedPlotfileMetadata.ts';
import {validatePlotfilePoint} from 'ROOT/studio/src/host/plotfileAudit.ts';
import {PROTOCOL_VERSION} from 'ROOT/studio/src/host/contracts.ts';
const path=process.argv[2],point=JSON.parse(process.argv[3]),query={field:'DENS',point};
const result=await readPlotfilePointIsolated(path,query);
const response=validatePlotfilePoint({protocolVersion:PROTOCOL_VERSION,projectId:'renderer-readback',relativePath:path,result},
 'renderer-readback',path,query,result.file.sha256);
console.log(JSON.stringify(response));
`;
await writeFile(output+'/fixture.jsx',fixture.replaceAll('ROOT',root));
await writeFile(output+'/index.html','<html><body style="margin:0"><div id="root"></div><script type="module" src="./fixture.jsx"></script></body></html>');
await writeFile(output+'/bridge.mjs',bridge.replaceAll('ROOT',root));
await writeFile(output+'/main.cjs',main.replaceAll('NODE',process.execPath));
await writeFile(output+'/preload.cjs',"const {contextBridge,ipcRenderer}=require('electron');contextBridge.exposeInMainWorld('probe',{point:query=>ipcRenderer.invoke('probe-point',query)});");
const {build}=await import(root+'/studio/node_modules/vite/dist/node/index.js');
const {default:react}=await import(root+'/studio/node_modules/@vitejs/plugin-react/dist/index.js');
await build({configFile:false,root:output,base:'./',plugins:[react()],build:{outDir:output+'/dist',emptyOutDir:false},logLevel:'error'});
const child=spawn(root+'/studio/node_modules/electron/dist/electron',['--no-sandbox','--disable-gpu',output+'/main.cjs'],{stdio:'inherit'});
const timer=setTimeout(()=>child.kill('SIGTERM'),45000);
const code=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',resolve);});
clearTimeout(timer);if(code!==0)throw Error('Renderer readback failed; original evidence retained');
const result=JSON.parse(await readFile(output+'/summary.json','utf8'));
assert.equal(result.status,'PASS');assert.equal(result.records.length,oracle.length);
for(const o of oracle)assert.equal(await sha(o.path),o.sha256);
console.log('PASS: '+output+'/summary.json');
