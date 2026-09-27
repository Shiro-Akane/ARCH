# Runtime control and resource lifetime

- [CudaBackendInternal.h](CudaBackendInternal.h) declares the complete backend
  ownership layout used by host-control translation units.
- [CudaBackendCore.cpp](CudaBackendCore.cpp) implements the common backend API.
- [CudaBackendResources.cpp](CudaBackendResources.cpp) owns allocations, streams,
  events and metadata transfers.
- [CudaBackendStore.cpp](CudaBackendStore.cpp) owns transactional store changes.
- [CudaBackendMicrophysicsControl.cpp](CudaBackendMicrophysicsControl.cpp)
  coordinates both burn and diffusion; its name does not imply burn-only ownership.

Synchronization and atomic publication belong to these control paths. Device
numerical templates do not: launch declarations live with their corresponding
functional group. The [runtime map](../README.md) identifies those boundaries.
