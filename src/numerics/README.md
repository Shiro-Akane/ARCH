# Numerical methods

This tree owns the numerical methods used by the fluid and microphysics
solvers. Start with the module matching the operation:

- [Reconstruction](reconstruction/README.md) builds face states and limits slopes.
- [Fluxes](flux/README.md) evaluate the selected Riemann or flux-splitting policy.
- [Hydro integration](integrator/README.md) combines fluxes, sources and stages.
- [Diffusion](diffusion/README.md) owns spatial operators and RKL integration.
- [Burn solvers](burnsolver/README.md) advance a selected reaction network.
- [Linear algebra](linalg/README.md) supplies matrix views and solver policies.

Mathematical expressions belong to these shared modules, not to a second CUDA
implementation. CPU traversal and CUDA launches bind the same policies;
allocation, synchronization and external sparse-provider calls remain backend
responsibilities. The common driver owns the overall operator schedule.

See the [Reference](../../docs/Reference.md),
[implementation ownership map](../../docs/development/ImplementationOwnership.md)
and [validation overview](../../validation/README.md).
