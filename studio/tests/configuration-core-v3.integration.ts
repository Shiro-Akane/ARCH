/** Explicit real-binary integration; no Build, Setup, Preview or simulation. */
import assert from 'node:assert/strict';
import {copyFile,readFile,writeFile,readdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import {ConfigurationAdapter} from '../host/configuration.ts';
import {previewFixture} from './preview-fixture.ts';
import {inputs,makeManifest,saveManifest} from '../host/buildManifest.ts';
const binary=process.argv[2];
if(!binary)throw new Error('Pass an explicitly built CPU ARCH binary.');
const f=await previewFixture();
try{
 await copyFile(resolve(binary),f.root+'/build/bin/ARCH');
 await saveManifest(f.p,await makeManifest(f.p,'p','real-config-v3-test',new Date().toISOString(),await inputs(f.p),undefined,{}));
 await f.build.initialize();
 const adapter=new ConfigurationAdapter(f.preview);
 const schema=await adapter.schema();assert.equal(schema.core.version,'3');
 assert.equal(schema.core.parameters.length,94);
 const text=await readFile(new URL('../../src/api/examples/configuration-v3/sod-valid.par',import.meta.url),'utf8');
 await writeFile(f.root+'/untouched.par','disk original');
 const before=await readdir(f.root);
 for(const [input,status,kind] of [[text,'ok','complete'],['','error','incomplete'],
  [text+'\nx_pos=0.25\nbad line\n','error','invalid'],
  [text+Array.from({length:30000},(_,i)=>'unknown_key_'+i+'=0\n').join(''),'error','undetermined']]){
  const result=await adapter.inspect({projectId:'p',caseId:'Sod',configText:input,
   configRevision:createHash('sha256').update(input).digest('hex')});
  assert.equal(result.core.status,status);
  assert.equal(result.core.completeness.state,kind);
  assert.equal(result.identity.binarySha256,schema.binarySha256);
  assert.equal(result.core.identity.configRevision,result.identity.configRevision);
  assert.equal(result.core.execution.setup,'not_executed');
  if(kind==='complete'){
   assert.equal(result.core.coordinates?.dimension,1);
   assert.equal(result.core.diffusion?.enabled,false);
   assert.equal(result.core.diffusion?.source,'constant');
   assert.equal(result.core.amrIndicators?.choices.find(c=>c.value==='DENS')?.selected,true);
   assert.equal(result.core.coordinates?.axes[0].unit,'cm');
  }
  if(kind==='incomplete'){
   assert.equal(result.core.coordinates,undefined);
   assert.equal(result.core.diffusion,undefined);
   assert.equal(result.core.amrIndicators,undefined);
   const value=result.core.parameters.find(p=>p.key==='cfl')!;
   assert.equal(value.parsedValue,null);assert.equal(value.resolvedValue,null);
  }
  if(kind==='undetermined'){
   assert.ok(result.core.diagnostics.some(d=>d.code==='RESPONSE_TOO_LARGE'));
   assert.equal(result.core.coverage.diagnosticsComplete,false);
  }
 }
 assert.equal(await readFile(f.root+'/untouched.par','utf8'),'disk original');
 assert.deepEqual(await readdir(f.root),before);
 console.log(JSON.stringify({status:'PASS',schemaVersion:schema.core.version,binarySha256:schema.binarySha256,
  cases:['valid','empty','duplicate-and-syntax','bounded-overflow'],scientificResources:'not-created',
  scope:'real binary through ConfigurationAdapter; isolated test Build Profile'},null,2));
}finally{await f.cleanup();}
