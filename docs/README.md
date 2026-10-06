# ARCH documentation

[中文](README.zh-CN.md) · [Project homepage](../README.md)

Begin with [build and first run](../README.md#build), then use the [case guide](guides/SimulationCase.md) to inspect output or write a problem. Inputs, outputs and physical constants use CGS.

## Choose features and settings

- [Feature list](Features.md): available modules, coordinates, boundaries and self-gravity domains.
- [Parameter and API reference](Reference.md): settings, method combinations, case interfaces and restart rules.
- [CUDA guide](CudaBackendStatus.md): backend choice and AMR execution.
- [Build guide](guides/Build.md): dependencies, presets, EOS tables and compile resources.
- [Simulation catalogue](../simulation/README.md): runnable problems and example inputs.
- [ARCH Studio](guides/Studio.md): Linux CLI desktop, parameter workspace, initial Preview/AMR and read-only Plotfile workflow.

## Interpret results

- [Validation index](../validation/README.md): module errors, conservation, coupling and backend checks.
- [Physics notes](physics/README.md) and [EOS tables](../EOS_toolkit/README.md): model and data provenance.
- [Release notes](releases/README.md): changes in identified source versions.
- [Legal and provenance index](legal/README.md): licenses for code, models and data.

## Develop and discuss

The [contributor guide](development/README.md) covers implementation ownership, ongoing gravity/GUI work and historical records. Use the [test guide](../tests/README.md) and [tool index](../tools/README.md) when editing source. The [reporting guide](guides/Reporting.md) helps prepare a reproducible build, runtime or numerical question.
