# P12 CPU full-domain gravity inputs

These CGS inputs run the existing `SNIaCoupled` and `GravityBox` cases from the
repository root. They exercise full-azimuth, isolated self-gravity through the
coordinate origin, axis, and poles; singular fluid faces are reflecting. CUDA
curvilinear self-gravity remains outside the accepted backend matrix.

| Input | Case | Purpose |
| --- | --- | --- |
| [p12_polar_origin.par](p12_polar_origin.par) | `SNIaCoupled` | 2D polar origin, 120-step four-module AMR run |
| [p12_spherical_2d_origin.par](p12_spherical_2d_origin.par) | `SNIaCoupled` | Native 2D spherical chart with the same `(r,phi)` polar semantics |
| [p12_cylindrical_3d_axis_mixed.par](p12_cylindrical_3d_axis_mixed.par) | `SNIaCoupled` | 3D cylindrical axis with coarse/fine leaves across the azimuth seam |
| [p12_spherical_3d_origin_poles_mixed.par](p12_spherical_3d_origin_poles_mixed.par) | `SNIaCoupled` | 3D spherical origin and both poles with mixed azimuth AMR |
| [p12_polar_low_density.par](p12_polar_low_density.par) | `GravityBox` | Near-vacuum polar-origin check using the existing `sml_rho` floor control |

For example:

```sh
./build-ci/cpu/bin/ARCH SNIaCoupled validation/gravity/curved/inputs/p12_polar_origin.par
```

The [acceptance record](../../results/p11-p12-20260923/README.md) distinguishes
manufactured field accuracy, short coupled runs, long evolution, restart and
regridding checks. These inputs do not model a complete white dwarf.
