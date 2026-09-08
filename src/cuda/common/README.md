# Common device infrastructure

- [CudaCommon.cuh](CudaCommon.cuh) and [DeviceStateFields.cuh](DeviceStateFields.cuh)
  provide device views and field access.
- [DeviceAllocation.h](DeviceAllocation.h) owns device allocations and error boundaries.
- [CudaLaunchConfig.h](CudaLaunchConfig.h) carries lightweight launch settings.
- [GridMetricsCache.h](GridMetricsCache.h) owns cached values from shared grid geometry.
- [DeviceEosStatus.h](DeviceEosStatus.h) transports EOS failures from kernels to control.

Keep these declarations independent of the complete runtime layout and network
catalogue. Allocation, indexing and error transport belong here; physics,
thermodynamic recovery and interpolation belong to their shared owners.
See the [runtime guide](../runtime/README.md) and [grid guide](../../grid/README.md).
