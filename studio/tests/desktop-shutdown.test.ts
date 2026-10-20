import test from 'node:test';
import assert from 'node:assert/strict';
import {createServer,request} from 'node:http';
import Module,{createRequire} from 'node:module';
import {closeAssets} from '../desktop/close-assets.mjs';
type WindowLike={isDestroyed:()=>boolean;setTitle:(title:string)=>void;destroy:()=>void};
type CloseOwned=(options:{window:()=>WindowLike;isDirty:()=>boolean;stopHost:()=>Promise<void>;closeAssets:()=>Promise<void>;quit:()=>void;showMessageBox:(target:WindowLike|undefined,options:{message:string;detail?:string})=>Promise<{response:number}>;log:(message:string)=>Promise<void>;setClosing:(value:boolean)=>void})=>Promise<void>;
function loadCloseOwned(){
 const loader=Module as unknown as {_load:(request:string,parent:unknown,isMain:boolean)=>unknown};
 const original=loader._load;
 const app={isPackaged:false,requestSingleInstanceLock:()=>true,on:()=>{},whenReady:()=>new Promise(()=>{}),getPath:()=>'/tmp',quit:()=>{}};
 const electron={app,BrowserWindow:class{},ipcMain:{handle:()=>{},on:()=>{}},dialog:{showMessageBox:async()=>({response:0})}};
 loader._load=function(request,parent,isMain){return request==='electron'?electron:original.call(this,request,parent,isMain);};
 try{return (createRequire(import.meta.url)('../desktop/main.cjs') as {closeOwned:CloseOwned}).closeOwned;}
 finally{loader._load=original;}
}
test('owned asset shutdown drains a live renderer connection without waiting for window destruction',async()=>{
 const server=createServer((_req,res)=>{res.writeHead(200);res.write('ongoing response');});
 await new Promise<void>(resolve=>server.listen(0,'127.0.0.1',resolve));
 const port=(server.address() as {port:number}).port;
 const req=request({host:'127.0.0.1',port});
 req.on('error',()=>{});
 const response=new Promise<void>(resolve=>req.once('response',res=>{res.on('error',()=>{});res.resume();resolve();}));
 req.end();await response;
 try{await closeAssets(server);assert.equal(server.listening,false);}
 finally{req.destroy();server.closeAllConnections();}
});
test('destroyed window cannot block owned Host and asset shutdown',async()=>{
 const closeOwned=loadCloseOwned();const calls:string[]=[];let closing=false;
 const window:WindowLike={isDestroyed:()=>true,setTitle:()=>calls.push('title'),destroy:()=>calls.push('destroy')};
 await closeOwned({window:()=>window,isDirty:()=>true,stopHost:async()=>{calls.push('host');},closeAssets:async()=>{calls.push('assets');},quit:()=>calls.push('quit'),showMessageBox:async()=>{calls.push('dialog');return {response:0};},log:async message=>{calls.push('log:'+message);},setClosing:value=>{closing=value;calls.push('closing:'+value);}});
 assert.deepEqual(calls,['closing:true','host','assets','log:desktop clean shutdown','quit']);
 assert.equal(closing,true);
});
test('live dirty window keeps the workbench open or proceeds only on discard',async()=>{
 const closeOwned=loadCloseOwned();
 const kept:string[]=[];let keptClosing=false;
 const live:WindowLike={isDestroyed:()=>false,setTitle:()=>kept.push('title'),destroy:()=>kept.push('destroy')};
 await closeOwned({window:()=>live,isDirty:()=>true,stopHost:async()=>{kept.push('host');},closeAssets:async()=>{kept.push('assets');},quit:()=>kept.push('quit'),showMessageBox:async()=>{kept.push('dialog');return {response:0};},log:async message=>{kept.push('log:'+message);},setClosing:value=>{keptClosing=value;kept.push('closing:'+value);}});
 assert.deepEqual(kept,['dialog']);assert.equal(keptClosing,false);
 const discarded:string[]=[];
 const discardedWindow:WindowLike={isDestroyed:()=>false,setTitle:()=>discarded.push('title'),destroy:()=>discarded.push('destroy')};
 await closeOwned({window:()=>discardedWindow,isDirty:()=>true,stopHost:async()=>{discarded.push('host');},closeAssets:async()=>{discarded.push('assets');},quit:()=>discarded.push('quit'),showMessageBox:async()=>{discarded.push('dialog');return {response:1};},log:async message=>{discarded.push('log:'+message);},setClosing:value=>{discarded.push('closing:'+value);}});
 assert.deepEqual(discarded,['dialog','closing:true','title','host','assets','log:desktop clean shutdown','destroy','quit']);
});
test('window destroyed during dirty confirmation still closes owned resources',async()=>{
 const closeOwned=loadCloseOwned();const calls:string[]=[];let inspections=0;
 const window:WindowLike={isDestroyed:()=>++inspections>1,setTitle:()=>calls.push('title'),destroy:()=>calls.push('destroy')};
 await closeOwned({window:()=>window,isDirty:()=>true,stopHost:async()=>{calls.push('host');},closeAssets:async()=>{calls.push('assets');},quit:()=>calls.push('quit'),showMessageBox:async()=>{calls.push('dialog');return {response:0};},log:async message=>{calls.push('log:'+message);},setClosing:value=>{calls.push('closing:'+value);}});
 assert.deepEqual(calls,['dialog','closing:true','host','assets','log:desktop clean shutdown','quit']);
});
test('resource errors are attempted and reported after window destruction',async()=>{
 const closeOwned=loadCloseOwned();const calls:string[]=[];let closing=true;
 const window:WindowLike={isDestroyed:()=>true,setTitle:()=>calls.push('title'),destroy:()=>calls.push('destroy')};
 await closeOwned({window:()=>window,isDirty:()=>false,stopHost:async()=>{calls.push('host');throw new Error('host down');},closeAssets:async()=>{calls.push('assets');},quit:()=>calls.push('quit'),showMessageBox:async(target,options)=>{calls.push('dialog:'+options.message);return {response:0};},log:async message=>{calls.push('log:'+message);},setClosing:value=>{closing=value;calls.push('closing:'+value);}});
 assert.deepEqual(calls,['closing:true','host','assets','closing:false','log:shutdown failed: host down','dialog:Shutdown failed; application could not close cleanly.']);
 assert.equal(closing,false);
});
