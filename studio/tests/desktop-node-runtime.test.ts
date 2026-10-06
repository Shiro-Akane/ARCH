import test from 'node:test';
import assert from 'node:assert/strict';
import {selectLinuxNode} from '../desktop/node-runtime.mjs';

test('Linux launcher selects supported Node from PATH, including spaced paths', async()=>{
 const seen:string[]=[];
 const selected=await selectLinuxNode({home:'/home/reader',searchPath:'/old:/path with spaces',probe:async(p:string)=>{seen.push(p);return p==='/old/node'?'v18.19.1':'v24.21.0';}});
 assert.equal(selected,'/path with spaces/node');
 assert.deepEqual(seen,['/old/node','/path with spaces/node']);
});
test('Explicit Linux Node failure is not replaced by another runtime', async()=>{
 const seen:string[]=[];
 await assert.rejects(selectLinuxNode({explicit:'/selected/node',home:'/home/reader',searchPath:'/working',probe:async(p:string)=>{seen.push(p);return 'v18.19.1';}}),/ARCH_STUDIO_NODE/);
 assert.deepEqual(seen,['/selected/node']);
});
test('Missing and relative Linux Node runtime are rejected',async()=>{
 await assert.rejects(selectLinuxNode({explicit:'relative/node',home:'/home/reader',probe:async()=> 'v24.21.0'}),/absolute Linux/);
 await assert.rejects(selectLinuxNode({home:'/home/reader',probe:async()=> {throw new Error('missing');}}),/Linux Node 24\+/);
});
test('Packaged Linux Host uses its bundled Node before system candidates',async()=>{
 const seen:string[]=[];
 const selected=await selectLinuxNode({bundled:'/package/runtime/node',home:'/home/reader',searchPath:'/system',probe:async(p:string)=>{seen.push(p);return 'v24.21.0';}});
 assert.equal(selected,'/package/runtime/node');
 assert.deepEqual(seen,['/package/runtime/node']);
});
