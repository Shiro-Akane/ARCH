import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {existsSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import ts from 'typescript';
import {createElement} from 'react';
import {renderToStaticMarkup} from 'react-dom/server';
import {initialState} from '../src/state/studioState.ts';
const cache=new Map<string,string>();
async function moduleUrl(url:URL):Promise<string>{
 const key=url.href;if(cache.has(key))return cache.get(key)!;
 const compiled=ts.transpileModule(await readFile(url,'utf8'),{compilerOptions:{module:ts.ModuleKind.ESNext,jsx:ts.JsxEmit.ReactJSX,target:ts.ScriptTarget.ES2022}}).outputText;
 let text=compiled;
 for(const match of compiled.matchAll(/from ["']([^"']+)["']/g)){
  const spec=match[1];let target:string;
  if(spec.startsWith('.')){let child=new URL(spec,url);if(!existsSync(fileURLToPath(child)))child=new URL(spec+'.tsx',url);if(!existsSync(fileURLToPath(child)))child=new URL(spec+'.ts',url);target=await moduleUrl(child);}else target=import.meta.resolve(spec);
  text=text.replaceAll(`from "${spec}"`,`from "${target}"`).replaceAll(`from '${spec}'`,`from '${target}'`);
 }
 const result='data:text/javascript;base64,'+Buffer.from(text).toString('base64');cache.set(key,result);return result;
}
async function component(path:string,name:string){return (await import(await moduleUrl(new URL('../src/'+path,import.meta.url))))[name];}
test('contextual Inspectors show only relevant data and parameter details',async()=>{
 const Inspector=await component('components/Inspector/Inspector.tsx','Inspector');
 const html=renderToStaticMarkup(createElement(Inspector,{state:initialState()}));assert.doesNotMatch(html,/>Composition</);assert.doesNotMatch(html,/>AMR level</);
 const Param=await component('components/Inspector/ParameterInspector.tsx','ParameterInspector');
 assert.match(renderToStaticMarkup(createElement(Param,{parameter:null})),/Select a parameter/);
 const real=renderToStaticMarkup(createElement(Param,{parameter:{key:'solver',label:'solver',value:'HLLC',raw:'HLLC',line:4,type:'text',options:['hllc']}}));
 assert.match(real,/Source line/);assert.match(real,/HLLC/);assert.match(real,/Allowed values/);assert.doesNotMatch(real,/Density/);
});
test('controls preserve raw fallback, bool tri-state and exact numeric values',async()=>{
 const Control=await component('components/ParameterPanel/ConfigControl.tsx','ConfigControl');
 const render=(props:object)=>renderToStaticMarkup(createElement(Control,{name:'custom',value:'',onChange:()=>{},...props}));
 assert.match(render({name:'solver',value:'future'}),/Unknown \/ raw value/);
 assert.match(render({name:'use_burn',value:'true',meta:{type:'bool'}}),/type="checkbox"/);
 assert.match(render({name:'use_nse',value:'auto',meta:{type:'text',options:['true','false','auto']}}),/<select/);
 assert.match(render({name:'tmax',value:'1.23456789e-8',meta:{type:'float'}}),/value="1.23456789e-8"/);
 const ranged=render({name:'refine_threshold',value:'0.7654321',meta:{type:'float',range:[0,1]}});assert.match(ranged,/type="range"/);assert.match(ranged,/value="0.7654321"/);
 assert.doesNotMatch(render({value:'0.5'}),/type="range"/);
});
