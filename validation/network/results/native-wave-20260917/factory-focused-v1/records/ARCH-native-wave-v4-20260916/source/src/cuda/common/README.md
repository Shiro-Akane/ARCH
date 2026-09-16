# Common device infrastructure

- [CudaCommon.cuh](CudaCommon.cuh) and [DeviceStateFields.cuh](DeviceStateFields.cuh)
  provide device views and field access.
- [DeviceAllocation.h](DeviceAllocation.h) owns device allocations and error boundaries.
- [CudaLaunchConfig.h](CudaLaunchConfig.h) carries lightweight launch settings.
- [GridMetricsCache.h](GridMetricsCache.h) owns cached values from shared grid geometry.
- [DeviceEosStatus.h](DeviceEosStatus.h) transports EOS failures from kernels to control.

You must maintain these declarations completely independent of the overarching runtime layout and the global network catalog. Device allocation, geometric indexing, and rigorous error transport explicitly belong here; conversely, all core physics, thermodynamic state recovery, and mathematical interpolation must remain with their shared owners. Further guidance can be found in the [runtime guide](../runtime/README.md) and the [grid guide](../../grid/README.md).
