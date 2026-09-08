# Runtime organization

The root contains the public [CudaBackend.h](CudaBackend.h), its
[factory](CudaBackendFactory.cpp), [lightweight records](CudaBackendTypes.h)
and [device store interface](DeviceBlockStore.h). Their include paths remain
stable; implementation files are grouped by responsibility:

| Group | Responsibility |
| --- | --- |
| [control](control/README.md) | Shared backend state, resource lifetime, store publication and cross-module control |
| [hydro](hydro/README.md) | Hydro launch declarations, stage control and EOS bindings |
| [burn](burn/README.md) | Dense/sparse burn launch contracts and registered network binding |
| [amr](amr/README.md) | Indicator, migration, ghost exchange and flux-correction orchestration |
| [diffusion](diffusion/README.md) | Diffusion launch interface and device execution |

These are functional boundaries, not separate implementations of physical
models. Numerical leaves remain shared with the CPU. Keep heavy EOS/network
bodies out of declaration-only launch headers; complete owners may include the
types they actually construct. Cross-group includes use the `cuda/runtime/...`
path from the existing `src` include root, without extra global search paths.

CMake continues to name each translation unit explicitly. Generated policy
bindings belong in the build directory and are produced by
[cmake templates](../../../cmake/README.md). The
[ownership map](../../../docs/development/ImplementationOwnership.md) explains
mathematical authorities and allowed adapters.
