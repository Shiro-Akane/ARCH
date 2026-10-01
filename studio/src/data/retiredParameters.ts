// Migration reservation from Core StandardParameters.h / Phase 2H contract.
// These names are not defaults, custom parameters, or editable aliases.
export const retiredParameters=new Set(['enforce_mass_conservation','burn_verbose_level','ode_use_numerical_jac','ode_freeze_jacobian','timeintegrator','gravity_G']);
export const isRetiredParameter=(key:string)=>retiredParameters.has(key);
