# Periodic diffusion mode

[DiffusionMode.cpp](DiffusionMode.cpp) registers `DiffusionMode`. It initializes
a periodic cosine species mode, with a cell-average correction for comparison
to analytic decay. Cartesian 1-D uses axis 1. Multidimensional cases must
declare `mode_axis`: any active Cartesian axis, or axial axis 2 in cylindrical
2-D/3-D. These multidimensional analytic cases require an unrefined root grid
and periodic faces on the selected axis. Cylindrical axis 2 is physical z.

The [diffusion input set](../../validation/diffusion/inputs) supplies the
constant-coefficient cases and resolutions. The
[diffusion summary](../../validation/diffusion/README.md) owns RKL configuration,
reference methods and accuracy budgets; inputs are not duplicated here.

The production driver supplies time stepping and diffusion fluxes. This model
only initializes the complementary species fractions; it owns no solver.
