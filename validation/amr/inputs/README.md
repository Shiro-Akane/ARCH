# AMR input sets

These parameter files are owned by [AMR validation](../README.md):

- [smooth_amr80_l1.par](smooth_amr80_l1.par),
  [smooth_uniform80.par](smooth_uniform80.par) and
  [smooth_uniform160.par](smooth_uniform160.par): an adaptive entropy wave with
  root-resolution and finest-spacing uniform controls.
- [sedov_amr_species.par](sedov_amr_species.par) and
  [sedov_amr_3d.par](sedov_amr_3d.par): Cartesian blast cases for multidimensional
  refinement, reflux and conservative transport.
- [gaussian_diffusion_amr.par](gaussian_diffusion_amr.par): localized species
  diffusion across an adaptive mesh.
- [burn_enuc_amr.par](burn_enuc_amr.par): burn-energy-driven refinement and restart.

[gpu_cases.json](../gpu_cases.json) and
[gpu_curvilinear_cases.json](../gpu_curvilinear_cases.json) define their execution
matrix and recorded overrides. Reproduce a campaign through its recipe so the
input identity, actual overrides and original budgets stay together.
