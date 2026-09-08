# Uniform burn problem

[BurnOneZone.cpp](BurnOneZone.cpp) registers `BurnOneZone`. It initializes a
uniform one-dimensional Cartesian state from density, temperature and network
composition, obtains pressure from the selected EOS, and uses the production
burn driver. Setup requires burning to be enabled.

Canonical [burn inputs](../../validation/burn/inputs/) and the
[burn summary](../../validation/burn/README.md) own the ODE comparisons and
thermal reference checks. Generated-network trajectories are documented in
[network validation](../../validation/network/README.md).

The problem supplies initial conditions only; reaction rates, EOS closure and
CPU/CUDA ODE algorithms are shared with other simulations.
