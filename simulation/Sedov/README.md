# Regularized Sedov blast

[Sedov.cpp](Sedov.cpp) registers `Sedov`. It deposits pressure within a finite
radius on a Cartesian grid, using the dimension-dependent injection measure.
The initializer samples cells at their centres; the
[hydro validation](../../validation/hydro/README.md) accounts for finite-radius
and spatial-resolution effects when comparing the blast with its reference.

[Sedov.par](Sedov.par) is the reusable example. The
[AMR validation](../../validation/amr/README.md) owns the qualified refinement,
coarsening and conservative-migration cases that reuse this problem.
