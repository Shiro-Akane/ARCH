import test from 'node:test';import assert from 'node:assert/strict';
import {registeredSourceCase} from '../host/desktopSource.ts';
test('desktop source association accepts real absolute and relative registry paths',()=>{
 const absolute=[{caseId:'Sod',inspection:{sourceFile:'/project/simulation/Sod/Sod.cpp'}}];
 assert.equal(registeredSourceCase(absolute,'/project','simulation/Sod/Sod.cpp'),'Sod');
 assert.equal(registeredSourceCase([{caseId:'Sod',inspection:{sourceFile:'simulation/Sod/Sod.cpp'}}],'/project','/project/simulation/Sod/Sod.cpp','Sod'),'Sod');
 assert.throws(()=>registeredSourceCase(absolute,'/project','other/Sod.cpp'));
 assert.throws(()=>registeredSourceCase(absolute,'/project','../other/Sod.cpp'));
 assert.throws(()=>registeredSourceCase(absolute,'/project','simulation/Sod/Sod.cpp','CellularDet'));
 assert.throws(()=>registeredSourceCase([{caseId:'Sod',inspection:{sourceFile:null}}],'/project','simulation/Sod/Sod.cpp'));
 assert.throws(()=>registeredSourceCase([...absolute,...absolute],'/project','simulation/Sod/Sod.cpp'));
});

import {desktopRegistry} from '../host/desktopSource.ts';
import {previewFixture} from './preview-fixture.ts';
import {ConfigurationAdapter} from '../host/configuration.ts';
import {WorkflowRunner} from '../host/workflow.ts';
import {readFile,writeFile} from 'node:fs/promises';
test('desktop static registry opens stale selected binary without enabling initialization',async()=>{
 const f=await previewFixture();
 try{
  const registry=JSON.parse(await readFile(new URL('../../src/api/examples/local-workflow/registered-cases.json',import.meta.url),'utf8'));
  const script='#!'+process.execPath+'\nif(process.argv[2]!=="--list-cases")process.exit(2);console.log('+JSON.stringify(JSON.stringify(registry))+');\n';
  await writeFile(f.root+'/build/bin/ARCH',script,{mode:0o755});
  await writeFile(f.root+'/case.cpp','changed tracked source');
  const configuration=new ConfigurationAdapter({root:f.root,projectId:'p',binaryRelativePath:'build/bin/ARCH'});
  const workflow=new WorkflowRunner(f.preview);
  const result=await desktopRegistry({configuration,workflow});
  assert.ok(result.cases.some(c=>c.caseId==='Sod'));
  assert.ok(result.buildId.startsWith('selected-binary:'));
  assert.deepEqual(result.fieldModels,[]);assert.equal(result.amr,null);
  assert.equal((await f.preview.readiness()).ready,false);
  await assert.rejects(workflow.discovery(),/Build required/);
  await assert.rejects(f.preview.start(f.request),/Build required/);
 }finally{await f.cleanup();}
});
