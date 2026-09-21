import test from 'node:test';
import assert from 'node:assert/strict';
import {standardValueError} from '../src/data/standardValidation.ts';
import {sameConfigurationIdentity} from '../src/host/configurationContracts.ts';
import type {StandardParameter,ConfigurationIdentity} from '../src/host/configurationContracts.ts';
const parameter=(type:StandardParameter['type']):StandardParameter=>({key:'test',type,group:'Grid',defaultValue:0,defaultSource:'shared-runtime-definition',constraints:{},options:null,units:{},path:null,applicability:'all'});
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
import {pairingSuspicion,previewMetadataMatches} from '../src/data/configurationIdentity.ts';
import {loadPar,editPar,parErrors,exportPar} from '../src/state/parState.ts';
test('actual Core schema and successful/failed inspection fixtures are accepted, malformed identity rejected',async()=>{
 const fixture=async(name:string)=>JSON.parse(await readFile(new URL('../../src/api/examples/configuration/'+name,import.meta.url),'utf8'));
 const schema=validateConfigurationSchema(await fixture('schema.json'));assert.equal(schema.parameters.length,90);
 for(const name of ['inspect-sod.json','inspect-cellular.json','invalid-integer.json']){const v=await fixture(name);validateConfigurationInspection(v,v.identity);assert.throws(()=>validateConfigurationInspection(v,{...v.identity,requestId:'late'}));}
 const broken=await fixture('schema.json');broken.parameters[1].key=broken.parameters[0].key;assert.throws(()=>validateConfigurationSchema(broken));
});
test('loaded and safely inserted standard integers share validation without rewriting unrelated bytes',async()=>{
 const schema=validateConfigurationSchema(JSON.parse(await readFile(new URL('../../src/api/examples/configuration/schema.json',import.meta.url),'utf8'))).parameters;
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

import {InspectionRequests} from '../src/data/inspectionRequests.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
test('late inspection cannot replace a newer model or a newer request for the same text',async()=>{
 const core=JSON.parse(await readFile(new URL('../../src/api/examples/configuration/inspect-sod.json',import.meta.url),'utf8'));
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
