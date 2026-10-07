# Gaussian species pulse

[Gaussian.cpp](Gaussian.cpp) registers `Gaussian`. One Gaussian envelope is
evaluated in physical Cartesian coordinates supplied by the grid and applied to
species and optional pressure/native-velocity perturbations. This keeps the
initial physical pulse consistent across the selected coordinate systems.

For public cylindrical 2D, the computational plane is `(r,z)`: set `yc=0`,
use `xc` for the radial center and `zc` for the axial center. The same existing
physical-coordinate envelope uses x=r, y=0, z=z. Its rotational extension is an
axisymmetric ring, rather than a Cartesian pulse centered outside the plane.
An actual r=0 domain additionally requires a physically regular scalar/velocity
profile at the axis; arbitrary nonzero radial or azimuthal velocity amplitudes
are not evidence of axis regularity. Initialization does not qualify full
Native RZ evolution, self-gravity, AMR or Device execution.

[Gaussian.par](Gaussian.par) is a reusable diffusion example. The
[AMR validation](../../validation/amr/README.md) owns the canonical geometry,
diffusion-coupling and dynamic-mesh cases; it also checks the initial envelope
against an independent reference.

This problem exclusively handles the initialization of fields. All underlying coordinate metrics, diffusion operators, and CPU/CUDA execution logic remain securely within their shared production modules.
