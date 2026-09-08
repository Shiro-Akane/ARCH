# Runtime control and resource lifetime

- [CudaBackendInternal.h](CudaBackendInternal.h) declares the complete backend
  ownership layout used by host-control translation units.
- [CudaBackendCore.cpp](CudaBackendCore.cpp) implements the common backend API.
- [CudaBackendResources.cpp](CudaBackendResources.cpp) owns allocations, streams,
  events and metadata transfers.
- [CudaBackendStore.cpp](CudaBackendStore.cpp) owns transactional store changes.
- [CudaBackendMicrophysicsControl.cpp](CudaBackendMicrophysicsControl.cpp)
  coordinates both burn and diffusion; its name does not imply burn-only ownership.

You must keep all synchronization primitives and atomic publication explicitly defined. This structural layout does not serve as a general-purpose include directory for device numerical templates. Instead, all launch declarations correctly belong in their respective sibling functional groups; please refer to the comprehensive [runtime map](../README.md) for clarification.
