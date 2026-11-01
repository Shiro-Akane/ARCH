# ARCH documentation

[中文](README.zh-CN.md) · [Project homepage](../README.md)

Start with the build guide and a small Sod calculation. ARCH uses CGS units for inputs, outputs and physical constants. Linux is recommended; Windows 11 supports the same CPU/CUDA and Studio workflows, with the tested configurations and limitations listed in [Platform support](releases/PlatformSupport.md).

## Getting started

| Task | Guide |
| --- | --- |
| Install dependencies and compile on Linux or Windows | [Build](guides/Build.md) |
| Run Sod, inspect output and create a problem | [Simulation cases](guides/SimulationCase.md) |
| Edit parameters, preview initial conditions and inspect output visually | [ARCH Studio](guides/Studio.md) |
| Select a physical model or numerical method | [Features](Features.md) |
| Look up a parameter, boundary or public interface | [Reference](Reference.md) |

The [simulation catalogue](../simulation/README.md) contains runnable examples. The [CUDA guide](CudaBackendStatus.md) explains backend selection; the [boundary guide](guides/UserBoundaries.md) explains custom fluid and potential conditions.

## Results and reproducibility

- [Validation](../validation/README.md): reference problems, error budgets, convergence and backend comparisons.
- [Physics notes](physics/README.md) and [EOS tables](../EOS_toolkit/README.md): scientific models and data sources.
- [Release notes](releases/README.md): version changes and compatibility.
- [Citation](zenodo/README.md) and [licenses](legal/README.md): attribution for software, code and data.

## Development and support

Use the [contributor guide](development/README.md), [tests](../tests/README.md) and [tools](../tools/README.md) when changing the implementation. The [reporting guide](guides/Reporting.md) explains how to share a reproducible build, runtime or numerical problem.
