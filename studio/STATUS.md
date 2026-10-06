# Studio integration status — 2026-10-06

Working integration: `compute/optim`, combining base `a566310e` and collaborator closure `b7cb8b69`. Current acceptance is recorded in the [integration report](../docs/development/ComputeStudioIntegrationReport-20261006.zh-CN.md), with the [release closure plan](../docs/development/ComputeStudioReleasePlan-20261006.zh-CN.md) as the next-step authority.

| Area | Current boundary |
| --- | --- |
| Linux CLI / desktop | Production `arch-studio` entry; opt-in CMake build, bundled Host Node in the Linux package; graphical and terminal prerequisites |
| Parameter workspace | Schema v3; explicit/conditional required inputs, unknown/retired/invalid diagnostics, case/source identity |
| Initial Preview / AMR | Real Setup/Init, bounded field/slice and separate AMR requests; capabilities negotiated per compiled case |
| Save / Build / Run / Restart | Explicit saving and tracked builds, independent owned run terminal, checkpoint identity and Core compatibility checks |
| Plotfile | Read-only candidate for supported 1D/2D layouts, display queries and native stored-cell inspection |
| Native UAT | Previous scoped evidence preserved; this integration does not substitute automation for missing desktop interaction checks |
| Private RZ external candidate | Colleague reports 24 evolution + 24 continuation PASS; artifact remains unapplied to public source |
| Full RZ findings | finite-ring BLOCKED; viscosity NOT_RUN; axis FAILED; continuous force BLOCKED |
| CUDA JENS / 2D diagnostics | Public CUDA gate retained; local GPU tests not run; 2D mapping/resource guard incomplete |

Incomplete intended-release functions must be completed and validated before release. Changing release scope requires an explicit project decision. `main` and release tags are not changed by this integration. Raw HDF5/checkpoint/log data stay local; only bounded reviewed summaries are versioned.

[Historical checkpoint reports](docs/archive/README.md) retain exact earlier scopes and limitations. Their counts and machine paths are not current launch instructions.
