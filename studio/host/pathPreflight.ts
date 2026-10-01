import {stat,lstat,access} from 'node:fs/promises';
import {constants} from 'node:fs';
import {resolve,dirname} from 'node:path';
import type {ConfigurationInspection,ConfigurationSchema,PathCheck} from '../src/host/configurationContracts.ts';
/** Metadata-only preflight. Never creates a directory or opens file contents. */
export async function pathPreflight(schema:ConfigurationSchema,inspection:ConfigurationInspection,cwd:string):Promise<PathCheck[]> {
 return Promise.all(schema.parameters.filter(p=>p.path?.checkOwner==='local-host'&&p.path.relativeTo==='process-working-directory'&&['input-file','output-directory'].includes(String(p.path.role))).map(async p=>{
  const role=p.path!.role as PathCheck['role'];const value=inspection.parameters.find(x=>x.key===p.key)?.parsedValue;
  const base:PathCheck={key:p.key,role,cwd,resolvedPath:null,status:'unable-to-check',message:'No current parsed path.'};
  if(typeof value!=='string')return base;
  if(!value)return {...base,status:'not-set',message:'Path not set.'};
  if(value.includes('\0'))return {...base,status:'error',message:'Invalid path token.'};
  const target=resolve(cwd,value);base.resolvedPath=target;
  try {
   let info;try{info=await stat(target);}catch(e){if((e as NodeJS.ErrnoException).code!=='ENOENT')throw e;}
   if(role==='input-file'){
    if(!info)return {...base,status:'error',exists:false,regularFile:false,readable:false,message:'Input file does not exist.'};
    if(!info.isFile())return {...base,status:'error',exists:true,regularFile:false,readable:false,message:'Input must be a regular file.'};
    try{await access(target,constants.R_OK);}catch{return {...base,status:'error',exists:true,regularFile:true,readable:false,message:'Input file is not readable.'};}
    return {...base,status:'ok',exists:true,regularFile:true,readable:true,message:'Existing readable regular file.'};
   }
   if(!info){try{await lstat(target);return {...base,status:'error',exists:true,message:'Output target is an unresolved symbolic link.'};}catch(e){if((e as NodeJS.ErrnoException).code!=='ENOENT')throw e;}}
   if(info&&!info.isDirectory())return {...base,status:'error',exists:true,message:'Output target is not a directory.'};
   let parent=info?target:dirname(target);
   while(true){try{const s=await stat(parent);if(!s.isDirectory())return {...base,status:'error',exists:false,parent,message:'Output ancestor is not a directory.'};break;}catch(e){if((e as NodeJS.ErrnoException).code!=='ENOENT'||dirname(parent)===parent)throw e;parent=dirname(parent);}}
   try{await access(parent,constants.W_OK|constants.X_OK);}catch{return {...base,status:'error',exists:!!info,parent,writable:false,message:'Output parent is not writable/searchable.'};}
   return {...base,status:'ok',exists:!!info,parent,writable:true,message:info?'Existing writable output directory.':'New output target; existing ancestor is writable. No directory created; runtime creation is not guaranteed.'};
  }catch(e){return {...base,status:'unable-to-check',message:`Unable to check path (${(e as NodeJS.ErrnoException).code??'filesystem error'}).`};}
 }));
}
