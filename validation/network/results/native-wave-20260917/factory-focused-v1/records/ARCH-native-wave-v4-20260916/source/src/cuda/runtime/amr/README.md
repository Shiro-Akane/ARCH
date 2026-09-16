# AMR runtime orchestration

- [CudaBackendIndicators.cpp](CudaBackendIndicators.cpp) binds selected refinement fields.
- [CudaBackendMigration.cpp](CudaBackendMigration.cpp) executes conservative
  device migration within a store transaction.
- [CudaBackendExchange.h](CudaBackendExchange.h) and its `.cu` implementation
  bind same-level and coarse/fine ghost exchange.
- [CudaBackendAmrFlux.h](CudaBackendAmrFlux.h) and its `.cu` implementation
  bind flux registration and reflux.

The CPU continues to manage grid topology and Morton ordering within the [amr](../../../amr/README.md) module. The specialized [CUDA AMR kernels](../../amr/README.md) strictly provide the device traversal of the universally shared indicator and transfer mathematics. Ensure that stage completion, retained storage lifecycles, and rollback rules are meticulously coordinated with the [runtime control](../control/README.md) architecture.
