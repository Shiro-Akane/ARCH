# AMR visual validation archive

This archive contains the reproducible renderer and AMR field figures generated
from stored HDF5 plot output. Rendering never changes solver state or HDF5 data.
Cartesian fields use physical cell quadrilaterals; spherical fields use polar
cell quadrilaterals. The mesh overlay contains leaf-patch boundaries only.

## Regenerate

From the ARCH repository root:

    work
    python Validation_file/AMR_Visual_Archive/render_amr_archive.py

## Inputs and figures

| Case | Figure | Plot input |
| --- | --- | --- |
| sedov | [sedov_amr.png](sedov_amr.png) | output/diag_sedov_dynamic_ppm_cfl03/SedovDynamicPPMCFL03_HLL_plt_0004.h5 |
| gaussian | [gaussian_amr.png](gaussian_amr.png) | output/diag_gaussian_amr_rkl_sts/GaussianAMRRKLSTSShort_HLL_plt_0002.h5 |
| rt | [rt_amr.png](rt_amr.png) | output/diag_rt_amr_gravity_diffusion/RTAMRGravityDiffusion_HLL_plt_0001.h5 |
| cellular | [cellular_amr.png](cellular_amr.png) | output/diag_cellular_amr_burn/CellularAMRBurn_HLL_plt_0005.h5 |
