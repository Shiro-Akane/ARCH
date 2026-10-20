import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {existsSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import ts from 'typescript';
import {createElement} from 'react';
import type {ReactNode} from 'react';
import {renderToStaticMarkup} from 'react-dom/server';

const cache=new Map<string,string>();
async function moduleUrl(url:URL):Promise<string>{
 const key=url.href;if(cache.has(key))return cache.get(key)!;
 let text=ts.transpileModule(await readFile(url,'utf8'),{compilerOptions:{module:ts.ModuleKind.ESNext,jsx:ts.JsxEmit.ReactJSX,target:ts.ScriptTarget.ES2022}}).outputText;
 for(const match of text.matchAll(/from ["']([^"']+)["']/g)){
  const spec=match[1];let target:string;
  if(spec.startsWith('.')){let child=new URL(spec,url);if(!existsSync(fileURLToPath(child)))child=new URL(spec+'.tsx',url);if(!existsSync(fileURLToPath(child)))child=new URL(spec+'.ts',url);target=await moduleUrl(child);}else target=import.meta.resolve(spec);
  text=text.replaceAll('from "'+spec+'"','from "'+target+'"').replaceAll("from '"+spec+"'","from '"+target+"'");
 }
 const result='data:text/javascript;base64,'+Buffer.from(text).toString('base64');cache.set(key,result);return result;
}
async function component(path:string,name:string){return (await import(await moduleUrl(new URL('../src/'+path,import.meta.url))))[name];}
async function providers(child:Parameters<typeof createElement>[0],props:object={}){
 const Build=await component('host/BuildProvider.tsx','BuildProvider');
 const Core=await component('state/coreParameters.tsx','CoreParameterProvider');
 return createElement(Build,null,createElement(Core,null,createElement(child,props)));
}

test('workflow presents every primary action once in one ordered action group',async()=>{
 const Bar=await component('components/WorkflowBar.tsx','WorkflowBar');
 const html=renderToStaticMarkup(await providers(Bar));
 const group=html.match(/<div class="workflow-primary-actions"[^>]*>(.*?)<\/div>/)?.[1];
 assert.ok(group,'main action group exists');
 assert.deepEqual([...group.matchAll(/<button[^>]*>(.*?)<\/button>/g)].map(m=>m[1]),['Configure','Build','Update Preview','Run','Restart from checkpoint','Terminal']);
 for(const action of ['Configure','Build','Update Preview','Run','Restart from checkpoint']){
  assert.match(group,new RegExp('<button disabled=""[^>]*>'+action+'</button>'));
 }
 assert.doesNotMatch(group,/role="status"/);
 assert.match(html,/<div class="workflow-status"[^>]*>/);
 assert.match(html,/Connect a Local Host\./);
});

test('workflow terminal is labelled, initially collapsed and independent of cancellation',async()=>{
 const Bar=await component('components/WorkflowBar.tsx','WorkflowBar');
 const html=renderToStaticMarkup(await providers(Bar));
 assert.match(html,/<button class="workflow-terminal-toggle" aria-expanded="false" aria-controls="workflow-terminal"[^>]*>Terminal<\/button>/);
 assert.match(html,/id="workflow-terminal"[^>]*hidden=""[^>]*aria-label="Configure \/ Build terminal"/);
 assert.match(html,/>Close terminal<\/button>/);
 assert.match(html,/closing this drawer does not cancel tasks/);
 assert.match(html,/No build output\./);
 assert.match(html,/No Configure output\./);
 assert.match(html,/Action availability/);
});

test('workflow placement keeps a single Run and Restart owner and its fallback UI',async()=>{
 const Controls=await component('components/RunControls.tsx','RunControls');
 const standalone=renderToStaticMarkup(await providers(Controls,{copy:null,busy:false}));
 assert.match(standalone,/<section class="run-controls" aria-label="Local Run and Restart">/);
 assert.match(standalone,/>Run<\/button>/);
 assert.match(standalone,/>Restart from checkpoint<\/button>/);
 const slotted=renderToStaticMarkup(await providers(Controls,{copy:null,busy:false,children:({actions,status,details}:{actions:ReactNode;status:ReactNode;details:ReactNode})=>createElement('div',null,createElement('nav',null,actions),createElement('aside',null,status,details))}));
 assert.equal((slotted.match(/>Run<\/button>/g)??[]).length,1);
 assert.equal((slotted.match(/>Restart from checkpoint<\/button>/g)??[]).length,1);
 assert.match(slotted,/<aside><span role="status">Connect a Local Host\.<\/span><\/aside>/);
});
