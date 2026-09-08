# Smooth periodic advection

[SmoothAdvection.cpp](SmoothAdvection.cpp) registers `SmoothAdvection`. It
initializes a one-dimensional Cartesian entropy wave with uniform pressure and
velocity, including cell-averaged density initialization.

Canonical parameter files live in [hydro inputs](../../validation/hydro/inputs/)
and [AMR inputs](../../validation/amr/inputs/). They exercise reconstruction
accuracy, transport across refinement boundaries and restart continuity using
the same initializer.

All reference methods and their associated results are rigorously maintained within the [hydro](../../validation/hydro/README.md) and [AMR](../../validation/amr/README.md) validation modules; no duplicate input sets are maintained in this directory.
