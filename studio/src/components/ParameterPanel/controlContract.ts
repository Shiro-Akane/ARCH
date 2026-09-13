// Presentation only. Do not feed these choices into parser or validation.
// Source: src/driver/dispatch/PolicyDescriptor.h registrations and parse_*.
const boundary = ['periodic', 'outflow', 'reflect', 'reflecting'];
export const enumControls: Record<string, readonly string[]> = Object.assign(Object.create(null), {
  geometry: ['cartesian', 'cylindrical', 'spherical'],
  compute_backend: ['cpu', 'cuda', 'auto'],
  solver: ['vl', 'vanleer', 'sw', 'stegerwarming', 'roe', 'hll', 'hllc'],
  reconstruct: ['pcm', 'donor_cell', 'plm', 'muscl', 'ppm'],
  limiter: ['minmod', 'mc', 'superbee', 'vanleer'],
  time_integrator: ['euler', 'rk1', 'rk2', 'ssprk2', 'rk3', 'ssprk3'],
  timeintegrator: ['euler', 'rk1', 'rk2', 'ssprk2', 'rk3', 'ssprk3'],
  eos_type: ['ideal', 'helmholtz', 'tabular'],
  x1l_boundary_type: boundary, x1r_boundary_type: boundary,
  x2l_boundary_type: boundary, x2r_boundary_type: boundary,
  x3l_boundary_type: boundary, x3r_boundary_type: boundary,
});
export function rawOption(value: string, options: readonly string[]) {
  return options.includes(value) ? null : {value, label: `${value} (${options.includes(value.toLowerCase()) ? 'loaded spelling' : 'Unknown / raw value'})`};
}
