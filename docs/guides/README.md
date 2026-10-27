# Task guides

[Build.md](Build.md) ([Chinese](Build.zh-CN.md)) explains configuration versus
compilation, dependencies, CUDA targets, and memory-aware build parallelism.

[Studio.md](Studio.md) ([Chinese](Studio.zh-CN.md)) covers Linux/WSL and native Windows in one workflow,
model selection, parameter Help/Inspector, Preview, builds, Run/Restart, Plotfile loading and host-specific paths. Its
[environment guide](StudioEnvironment.md) lists desktop prerequisites.

Linux is recommended. Native Windows11 x64 has the corresponding CPU/CUDA CLI and Studio functionality within the existing scientific scope, but may still have instability or bugs. Windows [dependencies](WindowsNativeW0.md), [CPU](WindowsNativeCpu.md), [CUDA](WindowsNativeCuda.md) and [desktop](WindowsStudioDesktop.md) recipes supplement the shared guides. See the [final functional correspondence](../histroy/build/WindowsNativeCudaQualification-20261027.md) for actual acceptance and limits.

[SimulationCase.md](SimulationCase.md) ([Chinese](SimulationCase.zh-CN.md)) walks
through building, running and extending a small Sod problem. Start there for a
first calculation, then use the [simulation catalogue](../../simulation/README.md)
to choose another problem.

[Reporting.md](Reporting.md) ([Chinese](Reporting.zh-CN.md)) explains how to
report build, runtime and numerical problems, share reproducible examples and
protect research data on workstations and shared computing systems.

The [Reference](../Reference.md) owns the complete parameter and extension
contracts; the [CUDA guide](../CudaBackendStatus.md) explains backend selection.
Generated-network preparation lives with the
[network workflow](../../src/physics/network/custom/README.md).

Keep this directory for task-oriented instructions. Scientific acceptance and
its reproducible inputs belong to [Validation](../../validation/README.md).

[UserBoundaries.md](UserBoundaries.md) ([Chinese](UserBoundaries.zh-CN.md))
defines separate physical/potential sibling files, CGS conditions, stage time,
coordinate joins, CUDA callback costs and restart identity.
