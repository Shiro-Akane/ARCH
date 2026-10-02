import test from 'node:test';
import assert from 'node:assert/strict';
import {createServer,request} from 'node:http';
import {closeAssets} from '../desktop/close-assets.mjs';
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
