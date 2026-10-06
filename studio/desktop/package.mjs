import {packager} from '@electron/packager';
import {mkdir,cp,writeFile,readFile,rm} from 'node:fs/promises';
import path from 'node:path';
const base=process.cwd(),staging=path.join(base,'.local','desktop-app');
const manifest=JSON.parse(await readFile(path.join(base,'package.json'),'utf8'));
if(manifest.name!=='arch-studio')throw new Error('Package ARCH Studio from its studio directory.');
if(process.platform!=='linux'||process.arch!=='x64'||Number(process.versions.node.split('.')[0])<24)
 throw new Error('Linux x64 desktop packaging requires Linux Node 24+ for the bundled Host.');
await rm(staging,{recursive:true,force:true});
await mkdir(staging,{recursive:true});
for(const file of ['main.cjs','close-assets.mjs','preload.cjs','arguments.mjs','node-runtime.mjs','launcher.html','launcher.js','launcher.css'])await cp(path.join(base,'desktop',file),path.join(staging,file));
for(const dir of ['dist','host','src'])await cp(path.join(base,dir),path.join(staging,dir),{recursive:true,filter:source=>!source.endsWith('.md')});
// Host Plotfile workers decode HDF5 outside the frontend bundle.
await mkdir(path.join(staging,'node_modules'),{recursive:true});
await cp(path.join(base,'node_modules','h5wasm'),path.join(staging,'node_modules','h5wasm'),{recursive:true});
await mkdir(path.join(staging,'runtime'),{recursive:true});
await cp(process.execPath,path.join(staging,'runtime','node'));
await writeFile(path.join(staging,'package.json'),JSON.stringify({name:'arch-studio',version:process.env.ARCH_STUDIO_VERSION??manifest.version,type:'module',main:'main.cjs',description:'ARCH Studio Linux desktop with local Host',author:'ARCH Studio',license:'MIT'}));
await cp(path.join(base,'..','LICENSE'),path.join(staging,'LICENSE'));
await cp(path.join(base,'DEPENDENCY-LICENSES.json'),path.join(staging,'DEPENDENCY-LICENSES.json'));
const output=await packager({dir:staging,out:path.join(base,'.local','desktop-release'),name:'arch-studio-bin',platform:'linux',arch:'x64',electronVersion:'44.4.3',overwrite:true,asar:false,prune:false});
for(const destination of output){await cp(path.join(base,'desktop','arch-studio'),path.join(destination,'arch-studio'));await cp(path.join(base,'desktop','README.md'),path.join(destination,'README.md'));await cp(path.join(base,'DEPENDENCIES.md'),path.join(destination,'DEPENDENCIES.md'));await cp(path.join(base,'..','LICENSE'),path.join(destination,'LICENSE.arch'));}
console.log(output);
