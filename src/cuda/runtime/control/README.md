# Runtime control and resource lifetime

- [CudaBackendInternal.h](CudaBackendInternal.h) declares the complete backend
  ownership layout used by host-control translation units.
- [CudaBackendCore.cpp](CudaBackendCore.cpp) implements the common backend API.
- [CudaBackendResources.cpp](CudaBackendResources.cpp) owns allocations, streams,
  events and metadata transfers.
- [CudaBackendStore.cpp](CudaBackendStore.cpp) owns transactional store changes.
- [CudaBackendMicrophysicsControl.cpp](CudaBackendMicrophysicsControl.cpp)
  coordinates both burn and diffusion; its name does not imply burn-only ownership.

Keep synchronization and atomic publication explicit. This layout is not a
general-purpose include for device numerical templates. Launch declarations
live in the sibling functional groups; see the [runtime map](../README.md).
