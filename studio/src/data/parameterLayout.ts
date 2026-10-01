import type {ConfigurationInspection,CoordinateSystem} from '../host/configurationContracts.ts';
import {catalog} from './parameterCatalog.ts';
export type CatalogRow=ReturnType<typeof catalog>[number];
// Display order only. Schema remains the authority for membership and values.
const runtimeSections=[
 ['Execution',['compute_backend','time_integrator','solver','hll_wave_speed','reconstruct','limiter','cfl']],
 ['Termination',['tmax','max_steps']],
 ['Output',['out_dir','base_name','plt_variables','plt_dt','plt_dstep']],
 ['Checkpoint / Restart',['restart','restart_file','chk_dt','chk_dstep']],
] as const;
export function runtimeSection(key:string){return runtimeSections.find(([,keys])=>(keys as readonly string[]).includes(key))?.[0]??'Advanced repair / device / time';}
export function runtimeOrder(a:CatalogRow,b:CatalogRow){
 const keys=runtimeSections.flatMap(([,keys])=>[...keys]) as string[];
 const rank=(k:string)=>{const i=keys.indexOf(k);return i<0?keys.length:i;};
 return rank(a.parameter.key)-rank(b.parameter.key);
}
export function forbiddenParameters(inspection:ConfigurationInspection|undefined):Set<string>{
 return new Set([...(inspection?.diffusion?.forbiddenExplicitKeys??[]),...(inspection?.diagnostics.filter(d=>d.severity==='error'&&d.code==='INAPPLICABLE_PARAMETER'&&d.parameterKey).map(d=>d.parameterKey!)??[])]);
}
export function parameterPlacement(row:CatalogRow,rows:CatalogRow[],inspection:ConfigurationInspection|undefined,coordinates:CoordinateSystem|undefined,errors:Record<string,string>):'common'|'advanced'|'inactive'{
 const p=row.parameter,key=p.key;
 if(errors[key]||errors[row.sourceKey]||inspection?.diagnostics.some(d=>d.parameterKey===key&&d.severity==='error'))return 'common';
 const value=(k:string)=>rows.find(r=>r.parameter.key===k)?.value.toLowerCase();
 if(p.group==='Gravity'){
  if(key==='gravity_type')return 'common';
  const mode=value('gravity_type');
  if(mode==='none')return 'inactive';
  if(mode==='external'){
   const axis=['gravity_g_x','gravity_g_y','gravity_g_z'].indexOf(key);
   if(axis>=0)return coordinates?.axes[axis]?.active?'common':'inactive';
   if(['gravity_boundary','gravity_G','gravity_rtol','gravity_atol','gravity_max_cycles'].includes(key)||inspection?.parameters.find(x=>x.key===key)?.applicable===false)return 'inactive';
  }
  if(mode==='self'&&['gravity_g_x','gravity_g_y','gravity_g_z'].includes(key))return 'inactive';
  if(mode==='self'&&['gravity_rtol','gravity_atol','gravity_max_cycles'].includes(key))return 'advanced';
  if(inspection?.parameters.find(x=>x.key===key)?.applicable===false)return 'inactive';
 }
 if(p.group==='Diffusion'){
  if(key==='use_diffusion')return 'common';
  if(value('use_diffusion')==='false')return 'inactive';
  if(p.presentation?.enabledBy){
   const channel=inspection?.diffusion?.channels.find(c=>c.coefficientKey===key);
   if(!channel?.constantInputAllowed||value(p.presentation.enabledBy)!=='true')return 'inactive';
  }
  if(['diff_cfl','diff_max_stages'].includes(key))return 'advanced';
 }
 if(p.group==='Network'&&!['use_burn','network_name','use_nse','ode_solver','linear_solver','ode_rtol','ode_atol'].includes(key))return 'advanced';
 return 'common';
}
