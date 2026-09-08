# Gravitational sources

This directory provides the configured gravity policy and its source coupling.

- [IGravityPolicy.h](IGravityPolicy.h) is the host patch interface.
- [GravityDispatch.h](GravityDispatch.h) binds the resolved policy.
- [GravityNone.h](GravityNone.h) represents the disabled source.
- [ExternalGravity.h](ExternalGravity.h) traverses host patches;
  [ExternalGravitySource.h](ExternalGravitySource.h) owns the shared cell update.

The external constant-acceleration source uses the same mathematics on CPU and
CUDA; device kernels supply traversal and state access. A self-gravity field
solver is not implemented in this directory. Hydro stage scheduling remains
with the common integrator/driver.

See the [Reference](../../../docs/Reference.md) and
[gravity validation](../../../validation/gravity/README.md).
