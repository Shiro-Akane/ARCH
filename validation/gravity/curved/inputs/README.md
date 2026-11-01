# Curved full-domain gravity inputs

These CGS inputs run the existing `SNIaCoupled` and `GravityBox` cases.
Spherical 2D uses the polar `(r,phi)` chart; 3D cylindrical/spherical inputs
exercise the origin, axis and poles with reflecting singular faces.
Native axisymmetric cylindrical 2D is covered separately in the
[gravity guide](../../README.md#native-cylindrical-meridian).

| Input | Case | Purpose |
| --- | --- | --- |
| [p12_spherical_2d_origin.par](p12_spherical_2d_origin.par) | `SNIaCoupled` | Native 2D spherical chart with the same `(r,phi)` polar semantics |
| [p12_cylindrical_3d_axis_mixed.par](p12_cylindrical_3d_axis_mixed.par) | `SNIaCoupled` | 3D cylindrical axis with coarse/fine leaves across the azimuth seam |
| [p12_spherical_3d_origin_poles_mixed.par](p12_spherical_3d_origin_poles_mixed.par) | `SNIaCoupled` | 3D spherical origin and both poles with mixed azimuth AMR |

For example:

```sh
./build-cpu/bin/ARCH SNIaCoupled validation/gravity/curved/inputs/p12_spherical_2d_origin.par
```

The [curved-coordinate CPU results](../../results/p11-p12-20260923/README.md) distinguishes
manufactured field accuracy, short coupled runs, long evolution, restart and
regridding checks. The [curved-coordinate CUDA record](../../results/p13-20260924/README.md) adds device parity and performance. These
inputs do not model a complete white dwarf.
