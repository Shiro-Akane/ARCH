import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {existsSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import ts from 'typescript';
import {createElement} from 'react';
import {renderToStaticMarkup} from 'react-dom/server';
import {authoritativeAxisLabel,caseParameterMatches,compactParameterUnit,orderedParameterGroups,physicalUnit} from '../src/components/ParameterPanel/parameterPresentation.ts';
import type {ConfigurationSchema,ConfigurationInspection} from '../src/host/configurationContracts.ts';

const cache=new Map<string,string>();
async function moduleUrl(url:URL):Promise<string>{
 if(cache.has(url.href))return cache.get(url.href)!;
 const compiled=ts.transpileModule(await readFile(url,'utf8'),{compilerOptions:{module:ts.ModuleKind.ESNext,jsx:ts.JsxEmit.ReactJSX,target:ts.ScriptTarget.ES2022}}).outputText;
 let text=compiled;
 for(const match of compiled.matchAll(/from ["']([^"']+)["']/g)){
  const spec=match[1];let target:string;
  if(spec.startsWith('.')){
   let child=new URL(spec,url);
   if(!existsSync(fileURLToPath(child)))child=new URL(spec+'.tsx',url);
   if(!existsSync(fileURLToPath(child)))child=new URL(spec+'.ts',url);
   target=await moduleUrl(child);
  }else target=import.meta.resolve(spec);
  text=text.replaceAll('from "'+spec+'"','from "'+target+'"').replaceAll("from '"+spec+"'","from '"+target+"'");
 }
 const result='data:text/javascript;base64,'+Buffer.from(text).toString('base64');cache.set(url.href,result);return result;
}
const Catalog=(await import(await moduleUrl(new URL('../src/components/ParameterPanel/StandardCatalog.tsx',import.meta.url)))).StandardCatalog;
const schema=JSON.parse(await readFile(new URL('../../src/api/examples/configuration-v3/schema.json',import.meta.url),'utf8')) as ConfigurationSchema;
function render(extra:object={},selected:ConfigurationSchema=schema){
 return renderToStaticMarkup(createElement(Catalog,{schema:selected,values:{geometry:'cartesian',nblockx1:'1',nblockx2:'1',nblockx3:'0'},inspection:undefined,pathChecks:undefined,errors:{},onEdit:()=>{},onSelect:()=>{},...extra}));
}
test('Runtime leads persistent groups; future groups and unified Case remain reachable',()=>{
 assert.deepEqual(orderedParameterGroups(['Case','Grid','Future','Runtime','EOS'],true),['Runtime','Grid','EOS','Case','Future']);
 const future={...schema.parameters[0],key:'future_key',group:'Future'};
 const combined={...schema,parameters:[...schema.parameters,...schema.caseDeclarations.find(c=>c.caseId==='Sod')!.parameters,future]};
 const html=render({caseParameterCount:1,caseParameters:(query:string)=>query&&!caseParameterMatches('custom_keep','text','custom description',query)?null:createElement('input',{'data-case-key':'custom_keep','aria-label':'custom_keep'})},combined);
 const nav=html.match(/<nav[^>]*aria-label="Parameter groups"[\s\S]*?<\/nav>/)![0];
 assert.ok(nav.indexOf('Runtime')<nav.indexOf('Grid'));
 assert.ok(nav.includes('Case parameters'));assert.ok(nav.includes('Future'));
 assert.equal((html.match(/data-case-key="custom_keep"/g)||[]).length,1);
 const rendered=[...html.matchAll(/data-standard-key="([^"]+)"/g)].map(match=>match[1]);
 assert.deepEqual(rendered.sort(),combined.parameters.filter(p=>!p.aliasOf).map(p=>p.key).sort());
 assert.equal(rendered.length,new Set(rendered).size);
 assert.match(html,/<details class="standard-group" open=""><summary>Runtime/);
});
test('coordinate system precedes stable axes and labels use Core without a guessed map',()=>{
 const html=render();
 assert.ok(html.indexOf('data-standard-key="geometry"')<html.indexOf('aria-label="Grid x1 axis"'));
 assert.match(html,/<h4>x<span class="axis-state">active/);
 assert.doesNotMatch(html,/<h4>x1 ·/);
 assert.equal(authoritativeAxisLabel(undefined),'Coordinate unavailable');
 const axis=schema.coordinateSystems![0].axes[0];
 assert.equal(authoritativeAxisLabel({...axis,displayName:'r',nativeName:'r_cy'}),'r');
 assert.match(html,/Core axis: x1/);
});
test('compact rows keep physical units; dimensionless or unknown units remain in Help',()=>{
 for(const unit of ['1','dimensionless','unknown','not-applicable',''])assert.equal(physicalUnit(unit),undefined);
 for(const status of ['unknown','dimensionless','not-applicable'])assert.equal(physicalUnit('cm',status),undefined);
 for(const unit of ['cm','rad','1/s','code_length'])assert.equal(physicalUnit(unit),unit);
 const bound=schema.parameters.find(p=>p.key==='x1_min')!;
 const c=schema.coordinateSystems!.find(c=>c.geometry==='cartesian'&&c.dimension===2)!;
 assert.equal(compactParameterUnit(bound,undefined,c),'cm');
 const html=render();
 const cfl=html.match(/data-standard-key="cfl"[\s\S]*?<\/details>/)![0];
 assert.doesNotMatch(cfl,/<small class="parameter-unit">/);
 assert.ok(cfl.indexOf('Unit: 1 · dimensionless')>cfl.indexOf('<details'));
 assert.match(cfl,/aria-label="Parameter help for cfl"/);
 assert.ok(cfl.indexOf('Schema Default:')>cfl.indexOf('<details'));
 assert.ok(cfl.indexOf('<p>Safety factor')>cfl.indexOf('<details'));
});
test('compaction keeps dependency errors, forbidden removal and Host path failure outside Help',async()=>{
 const raw=JSON.parse(await readFile(new URL('../../src/api/examples/configuration-v3/sod-valid.json',import.meta.url),'utf8')) as ConfigurationInspection;
 const inspected=structuredClone(raw);
 inspected.diagnostics=[{severity:'error',code:'INAPPLICABLE_PARAMETER',parameterKey:'alpha_therm',message:'Forbidden coefficient',module:null,conditionId:null,expected:{type:null,units:null},locations:[],relatedKeys:[]}];
 const unresolved=inspected.parameters.find(p=>p.key==='cfl')!;
 unresolved.applicability={conditionId:'needs-input',state:'unknown-dependency',missingDependencies:['required_input']};
 const html=render({inspection:inspected,values:{alpha_therm:'3',cfl:'bad'},errors:{cfl:'Invalid CFL'},onRemove:()=>{},pathChecks:[{key:'eos_table_path',role:'input-file',cwd:'/project',resolvedPath:'/missing',status:'error',message:'File missing'}]});
 const cfl=html.match(/data-standard-key="cfl"[\s\S]*?<\/details>/)![0];
 assert.ok(cfl.indexOf('Invalid CFL')<cfl.indexOf('<details'));
 assert.ok(cfl.indexOf('Applicability unresolved: required_input')<cfl.indexOf('<details'));
 const path=html.match(/data-standard-key="eos_table_path"[\s\S]*?<\/details>/)![0];
 assert.ok(path.indexOf('File missing')<path.indexOf('<details'));
 assert.match(html,/Remove forbidden parameter alpha_therm/);
 assert.match(html,/Inspection Parsed Value:/);assert.match(html,/Resolved Value:/);
});
test('unified Case search covers raw keys, friendly names, values and provided descriptions',()=>{
 assert.equal(caseParameterMatches('XH','0.7',undefined,'xh'),true);
 assert.equal(caseParameterMatches('x_pos','0.3','Discontinuity position','discontinuity'),true);
 assert.equal(caseParameterMatches('unknown','preserved',undefined,'preserved'),true);
 assert.equal(caseParameterMatches('unknown','text',undefined,'none'),false);
});
test('Diffusion editing follows Core explicit-forbidden authority and preserves unknown applicability',async()=>{
 const inspected=JSON.parse(await readFile(new URL('../../src/api/examples/configuration-v3/sod-valid.json',import.meta.url),'utf8')) as ConfigurationInspection;
 const coefficient=inspected.parameters.find(parameter=>parameter.key==='alpha_therm')!;
 inspected.diagnostics=[];
 if(inspected.diffusion)inspected.diffusion.forbiddenExplicitKeys=[];
 const row=(html:string)=>html.match(/data-standard-key="alpha_therm"[\s\S]*?<\/details>/)![0];
 const values={use_diffusion:'true',use_thermal_diff:'true',alpha_therm:'3'};
 coefficient.applicability={conditionId:'requires:alpha_therm',state:'satisfied',missingDependencies:[]};
 const permitted=row(render({values,inspection:inspected}));
 assert.doesNotMatch(permitted,/<fieldset[^>]*disabled/);
 coefficient.applicability={conditionId:'requires:alpha_therm',state:'unknown-dependency',missingDependencies:['transport_state']};
 const unknown=row(render({values,inspection:inspected}));
 assert.doesNotMatch(unknown,/<fieldset[^>]*disabled/);
 assert.ok(unknown.indexOf('Applicability unresolved: transport_state')<unknown.indexOf('<details'));
 inspected.diagnostics=[{severity:'error',code:'INAPPLICABLE_PARAMETER',parameterKey:'alpha_therm',message:'Explicit coefficient forbidden by Core',module:null,conditionId:null,expected:{type:null,units:null},locations:[],relatedKeys:[]}];
 const forbidden=row(render({values,inspection:inspected,onRemove:()=>{}}));
 assert.match(forbidden,/<fieldset[^>]*disabled/);
 assert.ok(forbidden.indexOf('Explicit coefficient forbidden by Core')<forbidden.indexOf('<details'));
 assert.match(forbidden,/Remove forbidden parameter alpha_therm/);
});
