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

These directories represent functional boundaries, not isolated implementations of physical models; all numerical leaves remain strictly shared with the CPU. You must keep heavy EOS and network bodies out of declaration-only launch headers, though complete owners are permitted to include the specific types they construct. For cross-group includes, always use the explicit `cuda/runtime/...` path starting from the existing `src` root, completely avoiding any extra global search paths.

CMake continues to name each translation unit explicitly. Generated policy
bindings belong in the build directory and are produced by
[cmake templates](../../../cmake/README.md). The
[ownership map](../../../docs/development/ImplementationOwnership.md) explains
mathematical authorities and allowed adapters.
