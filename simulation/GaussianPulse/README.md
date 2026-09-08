# Gaussian species pulse

[Gaussian.cpp](Gaussian.cpp) registers `Gaussian`. One Gaussian envelope is
evaluated in physical Cartesian coordinates supplied by the grid and applied to
species and optional pressure/native-velocity perturbations. This keeps the
initial physical pulse consistent across the selected coordinate systems.

[Gaussian.par](Gaussian.par) is a reusable diffusion example. The
[AMR validation](../../validation/amr/README.md) owns the canonical geometry,
diffusion-coupling and dynamic-mesh cases; it also checks the initial envelope
against an independent reference.

The problem only initializes fields. Coordinate metrics, diffusion operators and
CPU/CUDA execution remain in their shared production modules.
