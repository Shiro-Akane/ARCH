# Studio integration status — 2026-10-10

The working integration is `compute/optim`. The [release acceptance record](../docs/development/ComputeStudioReleasePlan-20261006.zh-CN.md#当前验收出口) contains current evidence and execution identities; the [O-series plan](../docs/development/ComputeOptimizationPlan.zh-CN.md#当前执行校准2026-10-08) defines the remaining release gates. Earlier integration reports retain their dated scope.

Studio is an optional Linux/WSL desktop component. Its production entry is `arch-studio`; the Linux package bundles Host Node. Graphical and terminal prerequisites are described in the [Studio guide](../docs/guides/Studio.md).

| Area | Current boundary |
| --- | --- |
| Configuration / Core API | Schema v3, explicit and conditional inputs, source identity, and invalid/retired input diagnostics. The real Host → Core static check covers 14 categories and 95 parameters; inspection precedes Setup and does not establish runtime readiness. |
| Initial Preview / AMR | Real Setup/Init, bounded fields/slices and separate AMR requests, with capabilities negotiated per compiled case. Native RZ provides an initialized root snapshot; a request for finer initial levels remains explicitly limited. |
| Source-bound workspace | A uniquely compiled model is fixed to its source. Conflicting identities return a typed error. Model discovery has an explicit Retry action that preserves the working copy. |
| Save / Build / Run / Restart | Native Configure, Build, Cellular Preview/AMR, and Sod Run/Restart have scoped evidence. The Sod continuation matches 19 state/geometry datasets byte for byte at the same endpoint. |
| Plotfile | Completed-publication and typed identity checks in a read-only pipeline. Native 1D/2D/3D point and slice queries expose intrinsic coordinates, cell bounds, volume/torque measures and CGS units. Overview/render supports Cartesian 1D/2D. Native Inspector, failed-publication handling and current-producer metadata reads have scoped engineering evidence. |
| Desktop acceptance | Actual Electron/Host paths were exercised on a private X11 display. The current bundle passed controlled discovery failure → Retry → real Host recovery and Cellular Preview. These automated desktop checks do not establish human WSLg foreground visibility. |
| Core mathematics / long runs | Finite static and Euler gravity checks, shared curved/native Stokes operators, Host FV/coarse-fine and public Host continuation have bounded acceptance. The independent RZ five-crossing endpoint and strict midpoint continuation remain pending. |
| CUDA | The final frozen-source build, original Device consumers and new production-binary public continuation remain pending. Historical GPU results retain their original identities. |
| Engineering checks | All 374 Studio tests pass with zero skips, using one test-file worker; lint, type checking and production build pass. The current CPU inventory passes 68 tests and Python tooling passes 749 tests, also with zero skips. These results retain their individual source/binary identities and do not substitute for pending scientific or performance gates. |
| CI / data | Main-targeting PR CI uses affected-path selection and the existing owners; long runs, formal timing and FLASH are local validation work. Raw HDF5/checkpoint/full logs stay local. Reviewed summaries are versioned after consumption and identity checks. The current workflow has local lint evidence; remote CI has not run for this candidate. |

Release readiness remains pending the remaining O-series gates. The current packaging pass closes usability evidence and records measured costs; further performance implementation is deferred. The project owner controls `main` merge and publication.

[Historical checkpoint reports](docs/archive/README.md) retain earlier counts, limitations and provenance. [Branch retirement](../docs/development/GuiBranchRetirement-20261006.zh-CN.md) records the old GUI delivery branches and recovery route.
