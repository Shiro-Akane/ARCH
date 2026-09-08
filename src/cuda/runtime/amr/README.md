# AMR runtime orchestration

- [CudaBackendIndicators.cpp](CudaBackendIndicators.cpp) binds selected refinement fields.
- [CudaBackendMigration.cpp](CudaBackendMigration.cpp) executes conservative
  device migration within a store transaction.
- [CudaBackendExchange.h](CudaBackendExchange.h) and its `.cu` implementation
  bind same-level and coarse/fine ghost exchange.
- [CudaBackendAmrFlux.h](CudaBackendAmrFlux.h) and its `.cu` implementation
  bind flux registration and reflux.

The CPU retains topology and Morton ordering in [amr](../../../amr/README.md).
[CUDA AMR kernels](../../amr/README.md) provide device traversal of the shared
indicator and transfer mathematics. Keep completion, retained storage and
rollback rules coordinated with [runtime control](../control/README.md).
