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

To trace a calculation end-to-end, start with the initialization in `main.cpp`, proceed to the stage scheduling in `driver/`, and finally examine the specific numerical or physical routines for the features you are interested in. Keep in mind that the exact same mathematical routine might be invoked either by a CPU loop or a CUDA kernel. If you are investigating memory allocation, kernel launches, or synchronization, follow the backend-specific execution paths; however, if you are modifying a physical equation or numerical method, focus directly on the shared routines.

Before adding any new implementations, always consult the [ownership map](../docs/development/ImplementationOwnership.md) to understand module boundaries. As a core design principle, both CPU and CUDA call the exact same mathematical bodies—only execution flow and resource management should be placed in backend-specific code.
Note that build and run instructions are maintained in the central [project guide](../README.md), rather than within the source directories.
