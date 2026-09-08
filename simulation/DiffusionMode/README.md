# Periodic diffusion mode

[DiffusionMode.cpp](DiffusionMode.cpp) registers `DiffusionMode`. It initializes
a cosine species mode on a one-dimensional Cartesian domain, with a
cell-average correction for comparison to analytic decay.

The [diffusion input set](../../validation/diffusion/inputs/) supplies the
constant-coefficient cases and resolutions. The
[diffusion summary](../../validation/diffusion/README.md) owns RKL configuration,
reference methods and accuracy budgets; inputs are not duplicated here.

Note that all time stepping and diffusion fluxes are inherently supplied by the production driver; this initializer problem does not implement those behaviors.
