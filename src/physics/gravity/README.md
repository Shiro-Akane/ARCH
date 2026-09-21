# Gravitational sources

This directory provides the configured gravity policy and its source coupling.

- [IGravityPolicy.h](IGravityPolicy.h) is the host patch interface.
- [GravityDispatch.h](GravityDispatch.h) binds the resolved `GravityId` through
  `make_gravity(config, GravityId)` and owns the compact disabled policy; names are parsed by the shared resolver.
- [ExternalGravity.h](ExternalGravity.h) traverses host patches;
  [ExternalGravitySource.h](ExternalGravitySource.h) owns the shared cell update.

The external constant-acceleration source uses the same mathematics on CPU and
CUDA; device kernels handle traversal and state access. Hydro stage scheduling
remains in the common integrator and driver modules.

[UniformGravity.h](UniformGravity.h) is the standalone P2 CPU adapter from a
borrowed Host density view to CGS potential and acceleration. It owns the
physical source and periodic density-mean removal; the Poisson operator and MG
cycle live in `numerics/elliptic` and `numerics/multigrid`. It supports one
uniform Cartesian domain with periodic or prescribed-potential boundaries.
It does not publish production fields or implement AMR/hydro coupling;
`gravity_type=self` remains rejected. See the
[P2 decisions](../../../docs/development/P2PoissonMultigrid.zh-CN.md).

See the [Reference](../../../docs/Reference.md) and
[gravity validation](../../../validation/gravity/README.md).
