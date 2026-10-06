# Studio integration status — 2026-10-07

Working integration: `compute/optim`, combining base `a566310e` and collaborator closure `b7cb8b69`. Current acceptance is recorded in the [integration report](../docs/development/ComputeStudioIntegrationReport-20261006.zh-CN.md), with the [release closure plan](../docs/development/ComputeStudioReleasePlan-20261006.zh-CN.md) as the next-step authority.

The shared development baseline is `origin/compute/optim`; old GUI delivery and contract branches are retired. New task branches start from its current reviewed commit. [Retirement/provenance](../docs/development/GuiBranchRetirement-20261006.zh-CN.md) preserves historical sources and the recovery route.

| Area | Current boundary |
| --- | --- |
| Linux CLI / desktop | Production `arch-studio` entry; opt-in CMake build, bundled Host Node in the Linux package; graphical and terminal prerequisites |
| Parameter workspace | Schema v3; explicit/conditional required inputs, unknown/retired/invalid diagnostics, case/source identity |
| Initial Preview / AMR | Real Setup/Init, bounded field/slice and separate AMR requests; capabilities negotiated per compiled case |
| Save / Build / Run / Restart | Explicit saving and tracked builds, independent owned run terminal, checkpoint identity and Core compatibility checks |
| Plotfile | Completed-publication and typed identity checks in the isolated read-only pipeline; bounded 1D/2D display queries, native cell inspection and CGS units. Current native desktop interaction acceptance is separate. |
| Native UAT | Previous scoped evidence preserved; this integration does not substitute automation for missing desktop interaction checks |
| Private RZ external candidate | Colleague reports 24 evolution + 24 continuation PASS; artifact remains unapplied to public source |
| Full RZ findings | Shared density/inertia closure, real post-ghost EOS, stage clocks and native output/checkpoint semantics have local CPU checks. Whole-model flux limiting, AMR, coupling, continuation and CUDA qualification remain open; continuous force reference is incomplete. |
| CUDA JENS / 2D diagnostics | Explicit Cartesian CUDA Jeans has an engineering route; fresh local GPU acceptance and performance are pending. Automatic and curved CUDA selection retain their rejection conditions. |
| Validation storage | Actual Host mount and bounded output guards; passed raw files are processed and identity-checked before cleanup. Failures and unqualified evidence remain local. |

Incomplete intended-release functions must be completed and validated before release. Changing release scope requires an explicit project decision. `main` and release tags are not changed by this integration. Raw HDF5/checkpoint/log data stay local; only bounded reviewed summaries are versioned.

[Historical checkpoint reports](docs/archive/README.md) retain exact earlier scopes and limitations. Their counts and machine paths are not current launch instructions.
