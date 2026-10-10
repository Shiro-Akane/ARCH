import test from 'node:test';
import assert from 'node:assert/strict';
import {standardValueError} from '../src/data/standardValidation.ts';
import {sameConfigurationIdentity} from '../src/host/configurationContracts.ts';
import type {StandardParameter,ConfigurationIdentity} from '../src/host/configurationContracts.ts';
const parameter=(type:StandardParameter['type']):StandardParameter=>({key:'test',type,group:'Grid',caseId:null,usage:'simulation',allowedDefault:null,templateRecommendations:[],requirement:{kind:'required',condition:{id:'always',dependencies:[],description:'Required'}},constraints:{},options:null,units:{status:'not-specified',unit:null},path:null,applicability:{id:'always',dependencies:[],description:'Always'}});
test('standard integer matches Core full-token and 32-bit contract',()=>{
 for(const s of ['1.5','1.0','1e2','12abc','2147483648','-2147483649','+-1',''])assert.ok(standardValueError(parameter('int'),s),s);
 for(const s of ['0','+1','-2147483648','2147483647'])assert.equal(standardValueError(parameter('int'),s),undefined,s);
});
test('finite numbers and documented expression grammar reject unsafe coercion',()=>{
 for(const s of ['NaN','Infinity','1e999','1e-999','12abc','0x10'])assert.ok(standardValueError(parameter('float'),s),s);
 for(const s of ['.5','1.','-1e-3','+2'])assert.equal(standardValueError(parameter('float'),s),undefined,s);
 for(const s of ['pi','-pi','2*pi','pi*2','pi/2','2 * pi'])assert.equal(standardValueError(parameter('expression'),s),undefined,s);
 for(const s of ['pi/0','pi+1','2*pi*2'])assert.ok(standardValueError(parameter('expression'),s),s);
});
test('late inspection identity requires every scope component',()=>{
 const a:ConfigurationIdentity={projectId:'p',caseId:'Sod',configRevision:'r',requestId:'q',buildId:'b',binarySha256:'sha'};
 assert.ok(sameConfigurationIdentity(a,{...a}));
 for(const key of Object.keys(a))assert.equal(sameConfigurationIdentity(a,{...a,[key]:'different'}),false,key);
});

import {readFile} from 'node:fs/promises';
import {validateConfigurationSchema,validateConfigurationInspection} from '../src/host/configurationValidation.ts';
import {pairingSuspicion,previewMetadataMatches,selectedSourceModels} from '../src/data/configurationIdentity.ts';
import type {ProjectSession} from '../src/host/contracts.ts';
import type {DiscoveryResponse} from '../src/host/workflowContracts.ts';
import {loadPar,editPar,parErrors,exportPar} from '../src/state/parState.ts';
test('actual Core schema and successful/failed inspection fixtures are accepted, malformed identity rejected',async()=>{
 const fixture=async(name:string)=>JSON.parse(await readFile(new URL('../../src/api/examples/configuration-v3/'+name,import.meta.url),'utf8'));
 const schema=validateConfigurationSchema(JSON.parse(await readFile(new URL('../../src/api/examples/configuration-v3/schema.json',import.meta.url),'utf8')));
 // The real selected Core owns catalog growth; scientific presence rules stay
 // explicit so refreshing response metadata cannot grant an implicit target.
 assert.equal(new Set(schema.parameters.map(p=>p.key)).size,schema.parameters.length);
 const jeans=schema.parameters.find(p=>p.key==='jeans_cells')!;
 assert.ok(jeans);assert.equal(jeans.allowedDefault,null);
 assert.equal(jeans.requirement.kind,'conditional');
 assert.deepEqual(jeans.requirement.condition.dependencies,['refine_var']);
 assert.equal(jeans.constraints.min,4);assert.equal(jeans.constraints.minInclusive,true);
 assert.equal(jeans.units.unit,'1');
 for(const name of ['sod-valid.json','empty.json','invalid.json']){const v=await fixture(name);validateConfigurationInspection(v,v.identity);assert.throws(()=>validateConfigurationInspection(v,{...v.identity,requestId:'late'}));}
 const broken=await fixture('schema.json');broken.parameters[1].key=broken.parameters[0].key;assert.throws(()=>validateConfigurationSchema(broken));
});
test('loaded and safely inserted standard integers share validation without rewriting unrelated bytes',async()=>{
 const schema=validateConfigurationSchema(JSON.parse(await readFile(new URL('../../src/api/examples/configuration-v3/schema.json',import.meta.url),'utf8'))).parameters;
 const absent=loadPar('1.par','# preserve\r\nnblockx2 = 0\r\nnblockx3 = 0\r\nunknown = keep\r\n');
 const present=loadPar('1.par',absent.document.raw+'ode_max_substeps = 10000\r\n');
 for(const raw of ['1.5','1.0','1e2','12abc','2147483648'])for(const state of [absent,present]){const edited=editPar(state,'ode_max_substeps',raw);assert.ok(parErrors(edited,schema).ode_max_substeps);assert.throws(()=>exportPar(edited,schema));}
 const edited=editPar(absent,'ode_max_substeps','42');assert.equal(parErrors(edited,schema).ode_max_substeps,undefined);const text=exportPar(edited,schema).text;assert.ok(text.startsWith(absent.document.raw));assert.match(text,/ode_max_substeps = 42/);assert.doesNotMatch(text,/ode_rtol/);
});
test('pairing suspicion is advisory; generic filenames never become verified',()=>{
 assert.ok(pairingSuspicion('CellularDet','Sod.par'));assert.ok(pairingSuspicion('Sod','CellularPreview2D.par'));
 for(const name of ['1.par','test.par','Sod.par'])assert.equal(pairingSuspicion('Sod',name),null);
 const scope={projectId:'p',buildId:'b',binarySha256:'sha'};const result={identity:{...scope,caseId:'Sod'}} as never;
 assert.ok(previewMetadataMatches(result,scope,'Sod'));assert.equal(previewMetadataMatches(result,scope,'CellularDet'),false);assert.equal(previewMetadataMatches(result,{...scope,binarySha256:'new'},'Sod'),false);
});

async function selectedSourceFixture(){
 const registry=JSON.parse(await readFile(new URL('../../src/api/examples/local-workflow/registered-cases.json',import.meta.url),'utf8'));
 const source='simulation/Sod/Sod.cpp',sourceSha='c'.repeat(64),binarySha='b'.repeat(64);
 const discovery:DiscoveryResponse={protocolVersion:PROTOCOL_VERSION,projectId:'project',buildId:'build',binarySha256:binarySha,cases:registry.cases,fieldModels:[],amr:null};
 const sod=discovery.cases.find(c=>c.caseId==='Sod')!;sod.inspection.sourceFile='/project/'+source;sod.inspection.compiledSourceSha256=sourceSha;
 const session:ProjectSession={projectId:'project',displayName:'project',projectRoot:'/project',caseSource:{relativePath:source,kind:'case-source',exists:true,changed:false,sha256:sourceSha},executable:{relativePath:'bin/ARCH',kind:'executable',exists:true,changed:false,sha256:binarySha},sourceState:'available',configFileState:'unknown',binaryState:'available',mapping:'unknown',metadata:'unavailable',openedAt:'now',refreshedAt:'now'};
 return {source,discovery,session,sod};
}
test('selected source presents only its unique compiled model and retains explicit reopening guidance',async()=>{
 const f=await selectedSourceFixture();
 for(const path of ['/project/'+f.source,f.source]){
  f.sod.inspection.sourceFile=path;
  const selection=selectedSourceModels(f.source,f.session,f.discovery,'Sod');
  assert.equal(selection.state,'fixed');assert.deepEqual(selection.cases.map(c=>c.caseId),['Sod']);
  if(selection.state==='fixed'){assert.equal(selection.caseId,'Sod');assert.match(selection.message,/Close this project and reopen/);}
 }
});
test('unregistered, ambiguous or stale selected source stays pending without a default Sod option',async()=>{
 const f=await selectedSourceFixture();
 const alias={...structuredClone(f.sod),caseId:'SodAlias'};
 // A second registration cannot be hidden by giving it a different digest:
 // the authoritative Host rejects path ambiguity before checking the SHA.
 alias.inspection.compiledSourceSha256='d'.repeat(64);
 const selections=[
  selectedSourceModels(f.source,f.session,{...f.discovery,cases:[]}),
  selectedSourceModels(f.source,f.session,{...f.discovery,cases:[f.sod,alias]}),
  selectedSourceModels(f.source,{...f.session,caseSource:{...f.session.caseSource!,sha256:'e'.repeat(64)}},f.discovery),
  selectedSourceModels(f.source,undefined,f.discovery),
  selectedSourceModels(f.source,f.session,null),
  selectedSourceModels(f.source,{...f.session,caseSource:{...f.session.caseSource!,sha256:undefined}},f.discovery),
  selectedSourceModels(f.source,f.session,{...f.discovery,projectId:'other'}),
  selectedSourceModels(f.source,f.session,{...f.discovery,binarySha256:'f'.repeat(64)}),
  selectedSourceModels(f.source,f.session,f.discovery,'CellularDet'),
 ];
 for(const selection of selections){assert.equal(selection.state,'pending');assert.deepEqual(selection.cases,[]);if(selection.state==='pending')assert.match(selection.message,/pending:/);}
});
test('unbound web model selection retains every registered model without a source identity',async()=>{
 const f=await selectedSourceFixture(),selection=selectedSourceModels(undefined,undefined,f.discovery);
 assert.equal(selection.state,'unbound');assert.equal(selection.cases.length,f.discovery.cases.length);assert.equal(selection.cases,f.discovery.cases);
 assert.ok(selection.cases.some(c=>c.caseId==='CellularDet'));assert.ok(selection.cases.some(c=>c.caseId==='ExternalGravity'));
});

import {InspectionRequests} from '../src/data/inspectionRequests.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
test('late inspection cannot replace a newer model or a newer request for the same text',async()=>{
 const core=JSON.parse(await readFile(new URL('../../src/api/examples/configuration-v3/sod-valid.json',import.meta.url),'utf8'));
 const scope={projectId:'p',buildId:'b',binarySha256:'sha'};
 const request={projectId:'p',caseId:'Sod' as const,configText:'unsaved',configRevision:core.identity.configRevision};
 core.identity.requestId='00000000-0000-0000-0000-000000000001';
 const response={protocolVersion:PROTOCOL_VERSION,identity:{...scope,...core.identity},core};
 const gate=new InspectionRequests();const old=gate.begin();gate.invalidate();const current=gate.begin();
 assert.equal(gate.accept(old,response,request,scope),null);
 assert.ok(gate.accept(current,response,request,scope));
 const changed={...response,identity:{...response.identity,caseId:'CellularDet'}};assert.throws(()=>gate.accept(current,changed,request,scope));
 gate.invalidate();assert.equal(gate.accept(current,response,request,scope),null);
});

test('path checks travel with inspection identity and cannot validate a newer revision',async()=>{
 const core=JSON.parse(await readFile(new URL('../../src/api/examples/configuration-v3/sod-valid.json',import.meta.url),'utf8'));
 const scope={projectId:'p',buildId:'b',binarySha256:'sha'};
 core.identity.requestId='00000000-0000-0000-0000-000000000001';
 const request={projectId:'p',caseId:'Sod' as const,configText:'old',configRevision:core.identity.configRevision};
 const response={protocolVersion:PROTOCOL_VERSION,identity:{...scope,...core.identity},core,pathChecks:[{key:'out_dir',role:'output-directory',cwd:'/managed',resolvedPath:'/managed/data',status:'ok',message:'Checked'}]};
 const gate=new InspectionRequests();const ticket=gate.begin();
 assert.equal(gate.accept(ticket,response,request,scope)?.pathChecks?.[0].resolvedPath,'/managed/data');
 assert.throws(()=>gate.accept(ticket,response,{...request,configRevision:'new'},scope),/identity/);
 gate.invalidate();assert.equal(gate.accept(ticket,response,request,scope),null);
 const schema=JSON.parse(await readFile(new URL('../../src/api/examples/configuration-v3/schema.json',import.meta.url),'utf8'));
 schema.parameters[0].units={status:'guessed',unit:'cm'};assert.throws(()=>validateConfigurationSchema(schema),/unit metadata/);
});

test('v3 rejects legacy fallback and false nullable/provenance/condition claims',async()=>{
 const fixture=JSON.parse(await readFile(new URL('../../src/api/examples/configuration-v3/empty.json',import.meta.url),'utf8'));
 validateConfigurationInspection(fixture,fixture.identity);
 for(const mutate of [
  (v:typeof fixture)=>v.version='2',
  (v:typeof fixture)=>v.parameters.find((p:{key:string})=>p.key==='cfl').parsedValue=0,
  (v:typeof fixture)=>v.parameters.find((p:{key:string})=>p.key==='cfl').valueSource='documented-default',
  (v:typeof fixture)=>v.parameters.find((p:{key:string})=>p.key==='cfl').resolvedValue=false,
  (v:typeof fixture)=>v.parameters.find((p:{key:string})=>p.key==='gamma').requirement.required=false,
  (v:typeof fixture)=>v.parameters.find((p:{key:string})=>p.key==='gamma').requirement.missingDependencies=[],
  (v:typeof fixture)=>v.status='ok',
  (v:typeof fixture)=>v.diagnostics[0].locations=[{source:'stdin',line:0,column:1,endColumn:2,rawValue:'x'}],
 ]){
  const bad=structuredClone(fixture);mutate(bad);assert.throws(()=>validateConfigurationInspection(bad,bad.identity));
 }
 const schema=JSON.parse(await readFile(new URL('../../src/api/examples/configuration-v3/schema.json',import.meta.url),'utf8'));
 schema.version='2';assert.throws(()=>validateConfigurationSchema(schema),/Incompatible.*Update Core and Studio/);
 schema.version='3';schema.parameters[0].defaultValue='cartesian';assert.throws(()=>validateConfigurationSchema(schema),/schema/);
});

test('draft persistence preserves empty, invalid, retired and duplicate text without granting validity',async()=>{
 const {exportDraft}=await import('../src/state/parState.ts');
 for(const raw of ['', '# incomplete\r\n', 'gravity_G = 1\r\nbad line\r\ncfl = nope\r\ncfl = 2\r\n']){
  const state=loadPar('draft.par',raw);assert.equal(exportDraft(state).text,raw);
 }
 const state=editPar(loadPar('draft.par','# original\n'),'cfl','nope');
 assert.ok(parErrors(state).cfl);assert.match(exportDraft(state).text,/cfl = nope/);
 assert.throws(()=>exportDraft(editPar(state,'cfl','1\ninjected=2')),/Value|Unsafe/);
});
