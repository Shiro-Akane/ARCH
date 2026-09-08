# Task guides

[Build.md](Build.md) ([Chinese](Build.zh-CN.md)) explains configuration versus
compilation, dependencies, CUDA targets, and memory-aware build parallelism.

[SimulationCase.md](SimulationCase.md) ([Chinese](SimulationCase.zh-CN.md)) walks
through building, running and extending a small Sod problem. Start there for a
first calculation, then use the [simulation catalogue](../../simulation/README.md)
to choose another problem.

The [Reference](../Reference.md) owns the complete parameter and extension
contracts; the [CUDA guide](../CudaBackendStatus.md) explains backend selection.
Generated-network preparation lives with the
[network workflow](../../src/physics/network/custom/README.md).

Keep this directory for task-oriented instructions. Scientific acceptance and
its reproducible inputs belong to [Validation](../../validation/README.md).
