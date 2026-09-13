export type CoreGroup = 'Grid'|'EOS'|'Network'|'Runtime';
export interface ParameterMeta { group: CoreGroup; type: 'text'|'int'|'float'|'bool'|'expression'; range?: readonly [number,number]; options?: readonly string[]; evidence: string }
export const parSchema: Record<string,ParameterMeta> = Object.create(null);
function add(group: CoreGroup, type: ParameterMeta['type'], keys: string[]) {
  for (const key of keys) parSchema[key]={group,type,evidence:'src/core/RuntimeParams.h: typed parser accessor'};
}
add('Grid','text',['geometry','x1_min','x1_max','x2_min','x2_max','x3_min','x3_max','x1l_boundary_type','x1r_boundary_type','x2l_boundary_type','x2r_boundary_type','x3l_boundary_type','x3r_boundary_type','refine_var']);
add('Grid','int',['nblockx1','nblockx2','nblockx3','max_blocks','lrefinemin','lrefinemax','regrid_interval']);
add('Grid','float',['refine_threshold','derefine_threshold']);
parSchema.refine_threshold.range=[0,1];
parSchema.refine_threshold.evidence='src/core/RuntimeParams.h: refine_threshold < 0 or > 1 rejected; derefine_threshold must be lower';
add('EOS','text',['eos_type','eos_table_path','eos_helm_table_path']);
add('EOS','float',['gamma']);
add('Network','bool',['use_burn','enforce_mass_conservation']);
add('Network','text',['network_name','use_nse']);
add('Network','float',['nuclearTempMin','nuclearDensMin','smallt','smallx','enucDtFactor','nseTempThreshold','nseDensThreshold']);
add('Network','int',['burn_verbose_level']);
add('Runtime','text',['compute_backend','solver','limiter','reconstruct','time_integrator','timeintegrator','out_dir','base_name','plt_variables','restart_file']);
add('Runtime','bool',['restart','EntropyFix','use_diffusion']);
add('Runtime','float',['cfl','tmax','plt_dt','chk_dt','EntropyFixCoefficient','sml_rho','min_eint','max_eint']);
add('Runtime','int',['max_steps','plt_dstep','chk_dstep']);

parSchema.use_nse.options=['true','false','auto'];

add('Grid','expression',['x1_min','x1_max','x2_min','x2_max','x3_min','x3_max']);
