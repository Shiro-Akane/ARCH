import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,writeFile,mkdir,chmod,rm,stat,readFile,symlink} from 'node:fs/promises';
import {tmpdir} from 'node:os';import {join} from 'node:path';
import {pathPreflight} from '../host/pathPreflight.ts';
const schema=JSON.parse(await readFile(new URL('../../src/api/examples/configuration/schema.json',import.meta.url),'utf8'));
const fixture=JSON.parse(await readFile(new URL('../../src/api/examples/configuration/inspect-sod.json',import.meta.url),'utf8'));
test('Host paths use process cwd, schema-only authority and never create output targets',async()=>{
 const root=await mkdtemp(join(tmpdir(),'arch-path-'));
 try{
  await writeFile(join(root,'table'),'data');await mkdir(join(root,'directory'));await writeFile(join(root,'blocked'),'secret');await chmod(join(root,'blocked'),0);
  for(const [value,expected] of [['table','ok'],['missing','error'],['directory','error'],['blocked','error'],['','not-set']]){
   const inspection=structuredClone(fixture);inspection.parameters.find((p:{key:string})=>p.key==='eos_table_path').parsedValue=value;
   inspection.parameters.find((p:{key:string})=>p.key==='out_dir').parsedValue='new/nested';
   inspection.parameters.push({key:'custom_file',parsedValue:'/etc/passwd'});
   const checks=await pathPreflight(schema,inspection,root);const input=checks.find(c=>c.key==='eos_table_path')!;
   assert.equal(input.status,expected);assert.equal(input.cwd,root);assert.equal(input.resolvedPath,value?join(root,value):null);assert.equal(checks.some(c=>c.key==='custom_file'),false);
   assert.equal(checks.find(c=>c.key==='out_dir')?.status,'ok');await assert.rejects(stat(join(root,'new')));
  }
  await symlink(join(root,'absent'),join(root,'dangling'));
  const inspection=structuredClone(fixture);inspection.parameters.find((p:{key:string})=>p.key==='out_dir').parsedValue='dangling';
  assert.equal((await pathPreflight(schema,inspection,root)).find(c=>c.key==='out_dir')?.status,'error');
  inspection.parameters.find((p:{key:string})=>p.key==='out_dir').parsedValue='table';
  assert.equal((await pathPreflight(schema,inspection,root)).find(c=>c.key==='out_dir')?.status,'error');
 }finally{await chmod(join(root,'blocked'),0o600);await rm(root,{recursive:true,force:true});}
});
