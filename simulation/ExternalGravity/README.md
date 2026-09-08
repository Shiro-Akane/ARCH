# Uniform external-gravity problem

[ExternalGravity.cpp](ExternalGravity.cpp) registers `ExternalGravity`. It
initializes a uniform one-dimensional Cartesian state for a configured constant
external acceleration. Setup requires `gravity_type=external`.

The [gravity inputs](../../validation/gravity/inputs/) select the time integrators.
The [gravity summary](../../validation/gravity/README.md) describes the analytic
momentum/energy comparison and coupled application results.

While this problem exercises the shared external-force update machinery, it does not attempt to solve a self-gravitating field, nor does it implement any custom source integrator.
