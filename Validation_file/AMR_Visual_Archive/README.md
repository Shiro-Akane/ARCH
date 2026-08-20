# Historical AMR visual archive

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

This archive contains an AMR renderer and historical field figures. Its status
is visual diagnostics with no quantitative tolerance. The canonical AMR status
and quantitative work list live in
[`validation/amr/README.md`](../../validation/amr/README.md).

Rendering never changes solver state or HDF5 data. Cartesian fields use physical
cell quadrilaterals; spherical fields use polar cell quadrilaterals. The mesh
overlay contains leaf-patch boundaries only.

## Regenerate

The renderer requires Python 3 with `numpy`, `h5py`, and `matplotlib`. From the
ARCH repository root:

    python -m pip install numpy h5py matplotlib
    python Validation_file/AMR_Visual_Archive/render_amr_archive.py

Fresh clones omit the ignored HDF5 runtime outputs listed below. Re-run the
corresponding cases or supply the plot files before invoking the renderer.

## Inputs and figures

| Case | Figure | Plot input |
| --- | --- | --- |
| sedov | [sedov_amr.png](sedov_amr.png) | output/diag_sedov_dynamic_ppm_cfl03/SedovDynamicPPMCFL03_HLL_plt_0004.h5 |
| gaussian | [gaussian_amr.png](gaussian_amr.png) | output/diag_gaussian_amr_rkl_sts/GaussianAMRRKLSTSShort_HLL_plt_0002.h5 |
| rt | [rt_amr.png](rt_amr.png) | output/diag_rt_amr_gravity_diffusion/RTAMRGravityDiffusion_HLL_plt_0001.h5 |
| cellular | [cellular_amr.png](cellular_amr.png) | output/diag_cellular_amr_burn/CellularAMRBurn_HLL_plt_0005.h5 |
