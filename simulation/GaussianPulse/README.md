# Gaussian species pulse

[Gaussian.cpp](Gaussian.cpp) registers `Gaussian`. One Gaussian envelope is
evaluated in physical Cartesian coordinates supplied by the grid and applied to
species and optional pressure/native-velocity perturbations. This keeps the
initial physical pulse consistent across the selected coordinate systems.

Two-dimensional cylindrical grids are unsupported. Spherical 2D retains the
polar `(r,phi)` plane; radial and 3D cylindrical initialization use the shared
physical-coordinate conversion.

[Gaussian.par](Gaussian.par) is a reusable diffusion example. The
[AMR validation](../../validation/amr/README.md) owns the canonical geometry,
diffusion-coupling and dynamic-mesh cases; it also checks the initial envelope
against an independent reference.

This problem exclusively handles the initialization of fields. All underlying coordinate metrics, diffusion operators, and CPU/CUDA execution logic remain securely within their shared production modules.
