# Curved full-domain gravity inputs

These CGS inputs run the existing `SNIaCoupled` and `GravityBox` cases from the
repository root. Cylindrical 2D represents full-ring axisymmetric `(r,z)`;
spherical 2D retains its polar `(r,phi)` chart. Three-dimensional curved inputs
exercise the coordinate origin, axis and poles with reflecting singular faces.

The four `rz_*.par` inputs are newly defined physical representatives. They
require current Native RZ science/backend qualification; historical polar
CPU/CUDA records describe a different source/geometry identity. Both RZ axes
use centimetres and physical axial faces. Their numerical, EOS and gas controls
are explicit and use the maintained same-model configuration values.

| Input | Case | Purpose |
| --- | --- | --- |
| [rz_origin.par](rz_origin.par) | `SNIaCoupled` | 2D full-ring RZ origin, four-module AMR representative; qualification pending |
| [p12_spherical_2d_origin.par](p12_spherical_2d_origin.par) | `SNIaCoupled` | Native 2D spherical chart with the same `(r,phi)` polar semantics |
| [p12_cylindrical_3d_axis_mixed.par](p12_cylindrical_3d_axis_mixed.par) | `SNIaCoupled` | 3D cylindrical axis with coarse/fine leaves across the azimuth seam |
| [p12_spherical_3d_origin_poles_mixed.par](p12_spherical_3d_origin_poles_mixed.par) | `SNIaCoupled` | 3D spherical origin and both poles with mixed azimuth AMR |
| [rz_low_density.par](rz_low_density.par) | `GravityBox` | Near-vacuum full-ring RZ check using the existing `sml_rho` floor control; qualification pending |
| [rz_origin_regular_4x4.par](rz_origin_regular_4x4.par) | `SNIaCoupled` | 4×4 root-block RZ regular-grid representative; independent CPU/CUDA qualification required |
| [rz_origin_amr_4x4.par](rz_origin_amr_4x4.par) | `SNIaCoupled` | Same new RZ physics and root layout with dynamic AMR requested; actual mixed leaves must be verified |

For example:

```sh
./build-ci/cpu/bin/ARCH SNIaCoupled validation/gravity/curved/inputs/rz_origin.par
```

The historical [curved-coordinate CPU acceptance](../../results/p11-p12-20260923/README.md) distinguishes
manufactured field accuracy, short coupled runs, long evolution, restart and
regridding checks. The [curved-coordinate CUDA record](../../results/p13-20260924/README.md) adds device parity and performance. These
inputs do not model a complete white dwarf.
