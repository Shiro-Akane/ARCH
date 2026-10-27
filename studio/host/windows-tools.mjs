/** Main-owned native prerequisites; never read from renderer or project data. */
import path from 'node:path';
import process from 'node:process';
import {stat} from 'node:fs/promises';

const required=['cmake','supervisor','ninja','objdump','publisher','rc'];
const local=value=>typeof value==='string'&&/^[A-Za-z]:[\\/]/.test(value)&&!value.includes('\0')&&path.win32.isAbsolute(value);
export function windowsSearchPath(value){
 if(typeof value!=='string'||!value||value.length>32767||!value.split(';').every(local))
  throw new Error('Windows search paths must contain only absolute local directories.');
 return value;
}
/** Main-private MSVC search directories, never inherited from project or HTTP data. */
export function compilerEnvironment(value){
 if(value===undefined)return {};
 if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).some(key=>!['INCLUDE','LIB','LIBPATH'].includes(key)))
  throw new Error('Invalid Windows compiler environment fields.');
 const result={};
 for(const [key,text] of Object.entries(value)){
  result[key]=windowsSearchPath(text);
 }
 return result;
}
export function parseWindowsTools(text){
 let value;
 try{value=JSON.parse(text);}catch{throw new Error('Set ARCH_STUDIO_WINDOWS_TOOLS to the trusted Windows tool configuration.');}
 if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).some(key=>![...required,'path','terminal','compilerEnvironment'].includes(key)))
  throw new Error('Invalid Windows tool configuration fields.');
 for(const key of [...required,...(value.terminal===undefined?[]:['terminal'])])
  if(!local(value[key])||!value[key].toLowerCase().endsWith('.exe'))throw new Error(`Windows ${key} must name an absolute local .exe.`);
 windowsSearchPath(value.path);
 return {...value,...(value.compilerEnvironment===undefined?{}:{compilerEnvironment:compilerEnvironment(value.compilerEnvironment)})};
}
export async function loadWindowsTools(text){
 const tools=parseWindowsTools(text);
 // Windows Terminal may be an App Execution Alias that rejects stat(). The
 // existing Run owner validates its explicit launch; it is not a Host prerequisite.
 for(const key of required){
  if(!(await stat(tools[key])).isFile())throw new Error(`Windows ${key} is not a file.`);
 }
 for(const dir of tools.path.split(';'))if(!(await stat(dir)).isDirectory())throw new Error('Windows tool PATH directory is unavailable.');
 for(const text of Object.values(compilerEnvironment(tools.compilerEnvironment)))
  for(const dir of text.split(';'))if(!(await stat(dir)).isDirectory())throw new Error('Windows compiler search directory is unavailable.');
 return tools;
}
export function windowsHostEnvironment(tools,environment=process.env){
 const env={PATH:tools.path,PATHEXT:'.COM;.EXE;.BAT;.CMD',...compilerEnvironment(tools.compilerEnvironment)};
 for(const key of ['SystemRoot','WINDIR','ComSpec','TEMP','TMP','USERPROFILE'])if(environment[key])env[key]=environment[key];
 return env;
}
