# Smooth periodic advection

[SmoothAdvection.cpp](SmoothAdvection.cpp) registers `SmoothAdvection`. It
initializes a periodic entropy wave with uniform pressure and velocity and
cell-averaged density. Cartesian 1-D uses axis 1. Multidimensional cases must
declare `mode_axis`: any active Cartesian axis, or axial axis 2 in cylindrical
2-D/3-D. These multidimensional analytic cases require an unrefined root grid
and periodic faces on the selected axis. Cylindrical axis 2 is physical z;
the spatially inactive phi direction is not used.

Canonical parameter files live in [hydro inputs](../../validation/hydro/inputs)
and [AMR inputs](../../validation/amr/inputs). They exercise reconstruction
accuracy, transport across refinement boundaries and restart continuity using
the same initializer.

The [hydro](../../validation/hydro/README.md) and
[AMR](../../validation/amr/README.md) summaries own reference methods and results.
The backend manifest includes the native cylindrical axial convergence group;
inputs are not duplicated in this directory.
