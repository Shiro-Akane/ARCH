# Regularized Sedov blast

[Sedov.cpp](Sedov.cpp) registers `Sedov`. It deposits pressure within a finite
radius on a Cartesian grid, using the dimension-dependent injection measure.
The initializer samples cells at their centres; the
[hydro validation](../../validation/hydro/README.md) accounts for finite-radius
and spatial-resolution effects when comparing the blast with its reference.

You can use [Sedov.par](Sedov.par) as a reusable example. Note that the [AMR validation](../../validation/amr/README.md) module exclusively owns all qualified refinement, coarsening, and conservative-migration test cases that leverage this problem's setup.
