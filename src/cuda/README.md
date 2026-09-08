# CUDA backend implementation

This directory adapts the shared CPU/CUDA mathematics to device execution.
It owns kernels, device storage, transfers, streams and CUDA library interfaces.

| Directory | Responsibility |
| --- | --- |
| [runtime](runtime/README.md) | Backend construction, functional orchestration and typed launch boundaries |
| [common](common/README.md) | Device views, allocation, geometry cache and error transport |
| [hydro](hydro/README.md) | Hydro cell/face kernels and boundary execution |
| [amr](amr/README.md) | Indicators, conservative migration, exchange and reflux kernels |
| [diffusion](diffusion/README.md) | Device traversal for shared diffusion operators |
| [microphysics](microphysics/README.md) | EOS/network storage and sparse burn execution |

Any mathematical changes belong exclusively in [numerics](../numerics/README.md), [physics](../physics/README.md), [grid](../grid/README.md), or [AMR](../amr/README.md). You must absolutely not duplicate those numerical formulas into this backend. For a complete list of supported configurations, consult the [capability guide](../../docs/CudaBackendStatus.md), and refer to the [CUDA tests](../../tests/cuda/README.md) for focused device checks.
