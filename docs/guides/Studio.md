# ARCH Studio local workflow

[中文](Studio.zh-CN.md) · [Homepage](../../README.md) · [Current status](../../studio/STATUS.md)

Studio is an optional desktop component; Core CPU/CUDA builds and runs work independently. It provides a parameter workspace, controlled build, real initial fields/AMR and read-only Plotfile queries. Scientific Core execution stays on Linux/WSL. Linux Node 24+ and a graphical session are required; Run/Restart also use `xterm` and Linux `flock`.

```bash
cmake --preset studio-cpu-release -DARCH_STUDIO_NODE=/absolute/path/to/node
cmake --build build-studio-cpu --parallel 2
build-studio-cpu/bin/arch-studio --project "$PWD" --binary build-studio-cpu/bin/ARCH --case Sod --config simulation/Sod/Sod.par
```

Core-only builds leave Studio disabled. The opt-in target prepares locked npm dependencies, Linux Electron and production assets; first preparation needs network/disk space. The generated launcher belongs to this source checkout. Existing Studio environments may run `studio/desktop/arch-studio` directly, with Node on PATH or `ARCH_STUDIO_NODE` set. Connections are internal; users do not start Vite or enter a port.

Choose a registered compiled case and its `.par`, edit grouped parameters, then request initial Preview/AMR. Unsaved working-copy Preview is allowed; Save/Save As and Run/Restart are explicit. With `--source`, the model is fixed to that source's unique compiled registration; close the project and reopen the intended source to change models. Unmatched source identity remains pending. If model discovery fails, use **Retry model discovery** for the current build; the working copy is retained. `--source` identifies C++ source and does not compile it. Source changes require Configure/Build. The managed CPU build profile uses `build-studio-cpu`; build dependency gaps stay unknown rather than being certified fresh.

Initial views call real Setup/Init and shared EOS conversion. Available dimensions/geometry/fields are negotiated per case; registration alone grants no Preview capability. AMR is a separate bounded request, overlaid only on matching field/config/build identities. Axisymmetric 2D `(r,z)` currently provides an initialized root-mesh snapshot; requests for refined levels are marked limited. This Preview scope is separate from dynamic AMR during evolution. Resource estimates describe capacity, not MPI OOM predictions. Coordinate and field/color scale controls are independent. Log views must distinguish zero/nonpositive values without changing raw data.

Core units are CGS, including ideal gas. Standard descriptions come from Core; custom units are reviewed source/declaration evidence, not automatic inference of arbitrary C++ expressions. See the [feature list](../Features.md) for supported model/backend combinations.

Confirmed Run/Restart use associated saved input and the selected compiled binary. Their owned independent terminal and records survive Host exit. Stop targets the recorded job. Output reservations coordinate managed runs but do not approve overwriting earlier scientific data. Cluster/background scheduling is outside this local workflow.

Plotfile views read full published files. Cartesian 1D/2D active leaves support display LOD. Formal published files additionally support exact stored-cell queries in their recorded native coordinates for Cartesian, cylindrical and spherical charts in 1D/2D/3D; bounds, V/W, units and provenance come from the file. Curved/3D rendering and physical qualification are separate. Initial samples are a separate source. XDMF, reusable spatial indexing/cache, more formats and complete native/scientific acceptance follow the [release closure plan](../development/ComputeStudioReleasePlan-20261006.zh-CN.md).

See [Core API](../../src/api/README.md), [configuration v3](../../src/api/docs/CONFIGURATION_API.md), [engineering tests](../../studio/tests/README.md) and [scientific validation](../../validation/README.md). [Historical reports](../../studio/docs/archive/README.md) retain earlier scopes; they are not current launch instructions.

See [environment setup](StudioEnvironment.md) for Linux prerequisites, installation and troubleshooting.
