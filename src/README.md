# Source guide

[main.cpp](main.cpp) loads a case, resolves its configuration and starts the
shared driver. Choose a directory by responsibility:

| Directory | Responsibility |
| --- | --- |
| [core](core/README.md) | Configuration, case registration and portability helpers |
| [interface](interface/README.md) | Problem setup and initialization contracts |
| [data](data/README.md) | State records and configuration types |
| [grid](grid/README.md) | Grid layout and shared physical geometry |
| [amr](amr/README.md) | Mesh topology, conservative transfers and exchange plans |
| [driver](driver/README.md) | Backend selection, stage scheduling and state lifetime |
| [numerics](numerics/README.md) | Shared numerical algorithms |
| [physics](physics/README.md) | Shared physical models, tables and constants |
| [cuda](cuda/README.md) | Device execution, storage and solver adapters |
| [io](io/README.md) | Configuration parsing, diagnostics and shared HDF5 IO |

Start with the [ownership map](../docs/development/ImplementationOwnership.md)
before adding an implementation. CPU and CUDA call the same mathematical bodies;
only execution and resource management belong in backend-specific code.
Build and run instructions live in the [project guide](../README.md), not here.
