import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {parseRuntimePe,validRuntimeEvidence,changedRuntimeInputs,readToolRuntimeEvidence} from '../host/toolRuntimeEvidence.ts';
import type {WindowsToolRuntimeEvidence} from '../host/toolRuntimeEvidence.ts';

const header='x.exe:     file format pei-x86-64\nMagic 020b (PE32+)\nEntry d 0000000000000000 00000000 Delay Import Directory\n';
function fixture():WindowsToolRuntimeEvidence{
 const file=(name:string)=>({path:'C:\\tools\\'+name,resolvedPath:'C:\\tools\\'+name,sha256:'a'.repeat(64),size:3});
 const searchInputs={applicationDirectories:['C:\\tools'],hostPath:['C:\\tools'],systemDirectory:'C:\\Windows\\System32'};
 return {kind:'static-tool-runtime',version:2,platform:'windows-x86_64',dependenciesComplete:false,
  roots:['C:\\tools\\x.exe'],inspectors:[file('objdump.exe')],searchInputs,
  searchInputsSha256:createHash('sha256').update(JSON.stringify(searchInputs)).digest('hex'),
  nodes:[{...file('x.exe'),format:'PE32+',machine:'AMD64',imports:['api-ms-test.dll'],delayImportState:'observed',delayImports:[]}],
  edges:[{parent:'C:\\tools\\x.exe',requested:'api-ms-test.dll',kind:'PE_IMPORT',resolution:'api-set-boundary',candidates:[]}],
  unresolved:[{parent:'C:\\tools\\x.exe',requested:'api-ms-test.dll',reason:'api-set-boundary',candidates:[]}],
  limitations:['dynamic-loads-unmodeled','windows-loader-order-not-modeled']};
}
test('PE imports and delay uncertainty are explicit; malformed/other formats reject',()=>{
 const pe=parseRuntimePe(header+'\tDLL Name: KERNEL32.dll\n');
 assert.deepEqual(pe.imports,['KERNEL32.dll']);assert.equal(pe.delayImportState,'observed');assert.deepEqual(pe.delayImports,[]);
 const delayed=parseRuntimePe(header.replace('0000000000000000','0000000000001234'));
 assert.equal(delayed.delayImportState,'unparsed');assert.equal(delayed.delayImports,undefined);
 for(const text of [header.replace('pei-x86-64','pei-i386'),header.replace('020b','010b'),header.replace(/Entry d[^\n]*\n/,''),header+'DLL Name: ../x.dll\n',header+'DLL Name: a.dll\nDLL Name: A.dll\n'])assert.throws(()=>parseRuntimePe(text));
});
test('PE metadata enforces graph references, bounds, candidate identities and unresolved boundaries',()=>{
 const valid=fixture();assert.equal(validRuntimeEvidence(valid),true);
 const mutations:((e:WindowsToolRuntimeEvidence)=>void)[]=[
  e=>{e.version=1 as never;},e=>{e.dependenciesComplete=true as never;},e=>{e.nodes.push(e.nodes[0]);},
  e=>{e.roots=['relative'];},e=>{e.roots=['C:\\absent.exe'];},e=>{e.nodes[0].sha256='x';},
  e=>{e.searchInputs.hostPath.reverse();e.searchInputs.hostPath.push('C:\\other');},e=>{e.inspectors=[];},
  e=>{e.edges[0].parent='C:\\absent.exe';},e=>{e.edges.push(e.edges[0]);},e=>{e.edges=[];},
  e=>{e.edges[0].resolvedPath='C:\\absent.dll';},e=>{e.edges[0].resolution='selected-prefix-file';},
  e=>{e.unresolved=[];},e=>{e.unresolved[0].reason='missing';},e=>{e.limitations=[];},
  e=>{e.nodes[0].delayImports=['missing.dll'];},e=>{e.nodes[0].delayImportState='unparsed';},e=>{e.edges[0].candidates=[{...e.inspectors[0],size:-1}];},
 ];
 for(const mutate of mutations){const bad=structuredClone(valid);mutate(bad);assert.equal(validRuntimeEvidence(bad),false,String(mutate));}
 const alias=structuredClone(valid);const vendor={...alias.nodes[0],path:'C:\\tools\\Vendor.dll',resolvedPath:'C:\\tools\\Vendor.dll',imports:[]};alias.nodes.push(vendor);alias.nodes[0].imports=['vendor.dll'];alias.edges=[{parent:alias.nodes[0].resolvedPath,requested:'vendor.dll',kind:'PE_IMPORT',resolution:'selected-prefix-file',candidates:[{...vendor,path:'C:\\tools\\vendor.dll'}],resolvedPath:vendor.resolvedPath}];alias.unresolved=[];assert.equal(validRuntimeEvidence(alias),true);
 const conflict=structuredClone(valid);conflict.edges[0].candidates=[{...conflict.nodes[0],sha256:'b'.repeat(64)}];conflict.unresolved[0].candidates=[conflict.nodes[0].path];assert.equal(validRuntimeEvidence(conflict),false);
 const repeated=structuredClone(valid);repeated.searchInputs.hostPath.push(repeated.searchInputs.hostPath[0]);repeated.searchInputsSha256=createHash('sha256').update(JSON.stringify(repeated.searchInputs)).digest('hex');assert.equal(validRuntimeEvidence(repeated),true);
 const drift=structuredClone(valid);drift.inspectors[0].sha256='b'.repeat(64);
 assert.ok(changedRuntimeInputs(valid,drift).includes(drift.inspectors[0].path));
 const search=structuredClone(valid);search.searchInputs.hostPath.push('C:\\new');search.searchInputsSha256=createHash('sha256').update(JSON.stringify(search.searchInputs)).digest('hex');
 assert.ok(changedRuntimeInputs(valid,search).includes('static tool runtime dependency graph'));
});
if(process.platform==='win32') test('actual native tool runtime uses inspected PE files, leaves system/API-set boundaries unknown', {skip:process.platform!=='win32'},async()=>{
 const tools={cmake:process.env.ARCH_TEST_CMAKE!,supervisor:process.env.ARCH_TEST_SUPERVISOR!,path:process.env.PATH!,objdump:process.env.ARCH_TEST_OBJDUMP!};
 assert.ok(tools.objdump);const evidence=await readToolRuntimeEvidence([tools.cmake],undefined,tools);
 assert.equal(evidence.version,2);assert.equal(validRuntimeEvidence(evidence),true);
 if(evidence.version!==2)throw Error('PE evidence required');
 assert.ok(evidence.nodes.length);assert.ok(evidence.edges.length);assert.ok(evidence.unresolved.length);
 assert.equal(evidence.dependenciesComplete,false);assert.ok(evidence.unresolved.some(u=>u.reason==='api-set-boundary'||u.reason==='system-boundary'));
 const again=await readToolRuntimeEvidence([tools.cmake],undefined,tools);assert.deepEqual(changedRuntimeInputs(evidence,again),[]);
});
