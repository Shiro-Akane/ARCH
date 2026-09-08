# Gravitational sources

This directory provides the configured gravity policy and its source coupling.

- [IGravityPolicy.h](IGravityPolicy.h) is the host patch interface.
- [GravityDispatch.h](GravityDispatch.h) binds the resolved `GravityId` through
  `make_gravity(config, GravityId)`; names are parsed by the shared resolver.
- [GravityNone.h](GravityNone.h) represents the disabled source.
- [ExternalGravity.h](ExternalGravity.h) traverses host patches;
  [ExternalGravitySource.h](ExternalGravitySource.h) owns the shared cell update.

The external constant-acceleration source is implemented using the exact same mathematics for both CPU and CUDA environments; device kernels simply handle spatial traversal and state access. Note that a self-gravity field solver is not provided in this directory. Any hydro stage scheduling logic properly remains within the common integrator and driver modules.

See the [Reference](../../../docs/Reference.md) and
[gravity validation](../../../validation/gravity/README.md).
