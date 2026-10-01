import {packager} from '@electron/packager';
import {mkdir,cp,writeFile} from 'node:fs/promises';
import path from 'node:path';
const base=process.cwd(),staging=path.join(base,'.local','desktop-app');
await mkdir(staging,{recursive:true});
for(const file of ['main.cjs','preload.cjs','arguments.mjs','launcher.html','launcher.js','launcher.css'])await cp(path.join(base,'desktop',file),path.join(staging,file));
for(const dir of ['dist','host','src'])await cp(path.join(base,dir),path.join(staging,dir),{recursive:true});
await writeFile(path.join(staging,'package.json'),JSON.stringify({name:'arch-studio',version:'0.16.0',type:'module',main:'main.cjs',description:'ARCH Studio Windows desktop with WSL Host',author:'ARCH Studio',license:'UNLICENSED'}));
await cp(path.join(base,'DEPENDENCY-LICENSES.json'),path.join(staging,'DEPENDENCY-LICENSES.json'));
const output=await packager({dir:staging,out:path.join(base,'.local','desktop-release'),name:'arch-studio',platform:'win32',arch:'x64',electronVersion:'44.4.3',overwrite:true,asar:false,prune:false});
for(const destination of output){await cp(path.join(base,'desktop','arch-studio'),path.join(destination,'arch-studio'));await cp(path.join(base,'desktop','README.md'),path.join(destination,'README.md'));await cp(path.join(base,'DEPENDENCIES.md'),path.join(destination,'DEPENDENCIES.md'));}
console.log(output);
