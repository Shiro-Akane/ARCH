# Numerical methods

This tree owns the numerical methods used by the fluid and microphysics
solvers. Start with the module matching the operation:

- [Reconstruction](reconstruction/README.md) builds face states and limits slopes.
- [Fluxes](flux/README.md) evaluate the selected Riemann or flux-splitting policy.
- [Hydro integration](integrator/README.md) combines fluxes, sources and stages.
- [Diffusion](diffusion/README.md) owns spatial operators and RKL integration.
- [Burn solvers](burnsolver/README.md) advance a selected reaction network.
- [Linear algebra](linalg/README.md) supplies matrix views and solver policies.
- [Cartesian Poisson](elliptic/CartesianPoisson.h) defines scalar geometry,
  physical boundary data, the operator and its compatible face gradient.
- [Host multigrid](multigrid/HostMultigrid.h) owns reusable auxiliary levels,
  signed transfers, the V-cycle and bounded coarse LU. The
  [P2 contract](../../docs/development/P2PoissonMultigrid.zh-CN.md) defines its
  current single-domain scope; it has no Driver, EOS or fluid-packing dependency.

Mathematical expressions strictly belong to these shared modules and must not be duplicated into a secondary CUDA implementation. Both CPU traversal loops and CUDA kernel launches bind the exact same numerical policies. Tasks like memory allocation, synchronization, and external sparse-provider calls remain the sole responsibility of their respective backend adapters, while the common driver orchestrates the overall operator schedule.

See the [Reference](../../docs/Reference.md),
[implementation ownership map](../../docs/development/ImplementationOwnership.md)
and [validation overview](../../validation/README.md).
