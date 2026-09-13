import type { CoreGroup } from '../../data/parSchema.ts';
export const coreBlocks: CoreGroup[] = ['Grid','EOS','Network','Runtime'];
export function observedDimension(values: Record<string,string>): 1|2|3|null {
  const a=values.nblockx2, b=values.nblockx3;
  if (a===undefined || b===undefined || !/^[+-]?\d+$/.test(a) || !/^[+-]?\d+$/.test(b)) return null;
  if(Number(a)<=0 && Number(b)>0)return null;
  return Number(a)<=0 ? 1 : Number(b)<=0 ? 2 : 3;
}
export function blockSummary(block: CoreGroup, values: Record<string,string>): string {
  if(block==='Grid') { const dim=observedDimension(values); return [values.geometry,dim ? `${dim}D` : null,values.nblockx1 ? `${values.nblockx1} blocks X` : null].filter(Boolean).join(' · ') || '—'; }
  if(block==='EOS')return values.eos_type || '—';
  if(block==='Network')return values.use_burn?.toLowerCase()==='false' ? 'Disabled' : values.network_name || '—';
  return [values.compute_backend,values.tmax ? `tmax ${values.tmax}` : null].filter(Boolean).join(' · ') || '—';
}

export function inactiveAxisKey(key: string, dimension: 1|2|3|null): boolean {
  if(dimension===null)return false;
  for(const axis of [1,2,3]) {
    const keys=[`nblockx${axis}`,`x${axis}_min`,`x${axis}_max`,`x${axis}l_boundary_type`,`x${axis}r_boundary_type`];
    if(axis>dimension && keys.includes(key))return true;
  }
  return false;
}

export const friendlyLabels: Record<string,string> = Object.assign(Object.create(null),{
 geometry:'Geometry',nblockx1:'X blocks',nblockx2:'Y blocks',nblockx3:'Z blocks',
 x1_min:'Minimum',x1_max:'Maximum',x2_min:'Minimum',x2_max:'Maximum',x3_min:'Minimum',x3_max:'Maximum',
 x1l_boundary_type:'Left',x1r_boundary_type:'Right',x2l_boundary_type:'Left',x2r_boundary_type:'Right',x3l_boundary_type:'Left',x3r_boundary_type:'Right',
 lrefinemin:'Minimum',lrefinemax:'Maximum',min_eint:'Minimum',max_eint:'Maximum',
 eos_type:'EOS type',network_name:'Network name',compute_backend:'Backend',tmax:'End time',max_steps:'Maximum steps',
});

export function advancedKey(key: string, group: CoreGroup, dimension: 1|2|3|null): boolean {
 if(group==='Grid') return inactiveAxisKey(key,dimension) || ['max_blocks','regrid_interval','refine_var','refine_threshold','derefine_threshold'].includes(key);
 if(group==='EOS')return ['eos_table_path','eos_helm_table_path'].includes(key);
 if(group==='Network')return !['use_burn','network_name','use_nse'].includes(key);
 return !['compute_backend','tmax','max_steps','solver','reconstruct','limiter','time_integrator','timeintegrator','cfl'].includes(key);
}
export function matchesParameter(key: string, value: string, query: string): boolean {
 const needle=query.trim().toLowerCase();
 return `${key} ${friendlyLabels[key] ?? ''} ${value}`.toLowerCase().includes(needle);
}
