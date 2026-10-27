import test from 'node:test';
import assert from 'node:assert/strict';
import {appendFile,mkdir,mkdtemp,rename,rm,symlink,writeFile} from 'node:fs/promises';
import {randomUUID} from 'node:crypto';
import os from 'node:os';
import path from 'node:path';
import {readSource,setSourceReadHooks} from '../host/source.ts';

const MAX_SOURCE_BYTES=256*1024;

async function tempRoot(prefix:string){return mkdtemp(path.join(os.tmpdir(),prefix));}

async function retryRename(from:string,to:string){
 for(let attempt=0;;attempt++){
  try{await rename(from,to);return;}
  catch(error){
   const code=(error as NodeJS.ErrnoException).code;
   if(attempt>=19||(code!=='EPERM'&&code!=='EACCES'&&code!=='EBUSY'))throw error;
   await new Promise(resolve=>setTimeout(resolve,10));
  }
 }
}

async function replaceByMove(target:string,content:string|Buffer){
 const temporary=target+'.replacement-'+randomUUID();
 const opened=target+'.opened-'+randomUUID();
 await writeFile(temporary,content);
 // Windows cannot rename over an open source handle, but can move that handle
 // aside. Completing both moves proves the checked path now names a new file.
 await retryRename(target,opened);
 await retryRename(temporary,target);
}

test('selected source returns exact UTF-8 with BOM, CRLF, Unicode and spaces',async()=>{
 const root=await tempRoot('arch-source-stable-');
 try{
  await mkdir(path.join(root,'case dir'));
  const relativePath='case dir/配置 输入.cpp';
  const text='\uFEFF// first\r\n// second ✓\r\n';
  await writeFile(path.join(root,'case dir','配置 输入.cpp'),text);
  const result=await readSource(root,relativePath,'project-1');
  assert.equal(result.projectId,'project-1');
  assert.equal(result.relativePath,relativePath);
  assert.deepEqual(Buffer.from(result.text,'utf8'),Buffer.from(text,'utf8'));
 }finally{await rm(root,{recursive:true,force:true});}
});

test('invalid UTF-8 is rejected instead of replaced',async()=>{
 const root=await tempRoot('arch-source-stable-');
 try{
  await writeFile(path.join(root,'invalid.cpp'),Buffer.from([0x2f,0x2f,0xc3,0x28,0x0a]));
  await assert.rejects(readSource(root,'invalid.cpp','project-1'));
 }finally{await rm(root,{recursive:true,force:true});}
});

test('256 KiB source is exact and one byte over is rejected',async()=>{
 const root=await tempRoot('arch-source-stable-');
 try{
  const atLimit='a'.repeat(MAX_SOURCE_BYTES);
  await writeFile(path.join(root,'at-limit.cpp'),atLimit);
  assert.equal((await readSource(root,'at-limit.cpp','project-1')).text,atLimit);
  await writeFile(path.join(root,'over-limit.cpp'),'a'.repeat(MAX_SOURCE_BYTES+1));
  await assert.rejects(readSource(root,'over-limit.cpp','project-1'),/256 KiB/);
 }finally{await rm(root,{recursive:true,force:true});}
});

test('traversal, absolute paths and backslash separators stay rejected',async()=>{
 const root=await tempRoot('arch-source-stable-');
 try{
  await writeFile(path.join(root,'case.cpp'),'// source\n');
  for(const selected of ['../case.cpp','nested/../case.cpp','/absolute.cpp','case\\file.cpp','nested//case.cpp'])
   await assert.rejects(readSource(root,selected,'project-1'));
  await assert.rejects(readSource(root,'nested/case.cpp','project-1'));
 }finally{await rm(root,{recursive:true,force:true});}
});

test('parent symlink or Windows junction cannot escape the project root',async()=>{
 const root=await tempRoot('arch-source-stable-');
 const outside=await tempRoot('arch-source-outside-');
 try{
  await writeFile(path.join(outside,'secret.cpp'),'// outside\n');
  await symlink(outside,path.join(root,'escape'),process.platform==='win32'?'junction':'dir');
  await assert.rejects(readSource(root,'escape/secret.cpp','project-1'));
 }finally{
  await rm(root,{recursive:true,force:true});
  await rm(outside,{recursive:true,force:true});
 }
});

test('replacement with equal bytes but a different file identity is rejected at open',async()=>{
 const root=await tempRoot('arch-source-stable-');
 try{
  const target=path.join(root,'case.cpp');
  const text='// equal bytes must not hide a replacement\r\n';
  await writeFile(target,text);
  let replaced=false;
  setSourceReadHooks({afterOpen:async()=>{await replaceByMove(target,text);replaced=true;}});
  await assert.rejects(readSource(root,'case.cpp','project-1'),/Source identity changed|Source changed while reading/);
  assert.equal(replaced,true);
 }finally{
  setSourceReadHooks({});
  await rm(root,{recursive:true,force:true});
 }
});

test('replacement during the read is rejected before returning text',async()=>{
 const root=await tempRoot('arch-source-stable-');
 try{
  const target=path.join(root,'case.cpp');
  await writeFile(target,'// original\r\n');
  let replaced=false;
  setSourceReadHooks({afterRead:async()=>{await replaceByMove(target,'// original\r\n');replaced=true;}});
  await assert.rejects(readSource(root,'case.cpp','project-1'),/Source identity changed|Source changed while reading/);
  assert.equal(replaced,true);
 }finally{
  setSourceReadHooks({});
  await rm(root,{recursive:true,force:true});
 }
});

test('growth after the bounded read is rejected',async()=>{
 const root=await tempRoot('arch-source-stable-');
 try{
  const target=path.join(root,'case.cpp');
  await writeFile(target,'// original\r\n');
  setSourceReadHooks({afterRead:()=>appendFile(target,'// appended\r\n')});
  await assert.rejects(readSource(root,'case.cpp','project-1'),/identity|changed/i);
 }finally{
  setSourceReadHooks({});
  await rm(root,{recursive:true,force:true});
 }
});
