# Common device infrastructure

- [CudaCommon.cuh](CudaCommon.cuh) and [DeviceStateFields.cuh](DeviceStateFields.cuh)
  provide device views and field access.
- [DeviceAllocation.h](DeviceAllocation.h) owns device allocations and error boundaries.
- [CudaLaunchConfig.h](CudaLaunchConfig.h) carries lightweight launch settings.
- [GridMetricsCache.h](GridMetricsCache.h) owns cached values from shared grid geometry.
- [DeviceEosStatus.h](DeviceEosStatus.h) transports EOS failures from kernels to control.

Keep these declarations independent of the runtime layout and network catalog.
This group owns device allocation, geometric indexing and error transport.
Physics, thermodynamic recovery and interpolation stay in shared modules.
See the [runtime guide](../runtime/README.md) and [grid guide](../../grid/README.md).
