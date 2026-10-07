# Studio integration status — 2026-10-08

Working integration: `compute/optim`, combining base `a566310e` and collaborator closure `b7cb8b69`. Current acceptance is recorded in the [integration report](../docs/development/ComputeStudioIntegrationReport-20261006.zh-CN.md), with the [release closure plan](../docs/development/ComputeStudioReleasePlan-20261006.zh-CN.md) as the next-step authority.

The shared development baseline is `origin/compute/optim`; old GUI delivery and contract branches are retired. New task branches start from its current reviewed commit. [Retirement/provenance](../docs/development/GuiBranchRetirement-20261006.zh-CN.md) preserves historical sources and the recovery route.

| Area | Current boundary |
| --- | --- |
| Linux CLI / desktop | Production `arch-studio` entry; opt-in CMake build, bundled Host Node in the Linux package; graphical and terminal prerequisites |
| Parameter workspace | Schema v3; explicit/conditional required inputs, unknown/retired/invalid diagnostics, case/source identity |
| Initial Preview / AMR | Real Setup/Init, bounded field/slice and separate AMR requests; capabilities negotiated per compiled case |
| Save / Build / Run / Restart | Explicit saving and tracked builds, independent owned run terminal, checkpoint identity and Core compatibility checks |
| Plotfile | Completed-publication and typed identity checks in the isolated read-only pipeline; formal native 1D/2D/3D point and slice queries use intrinsic coordinates, native bounds, volume/torque measures and CGS units. Overview/render remains Cartesian 1D/2D. Current native desktop interaction acceptance is separate. |
| Native UAT | Previous scoped evidence preserved; this integration does not substitute automation for missing desktop interaction checks |
| Native RZ Host engineering | The integrated shared density/inertia closure, real post-ghost EOS, stage clocks and native output/checkpoint semantics have local CPU checks. Actual four-module mixed-AMR evolution and fresh checkpoint continuation have bounded engineering evidence; they do not establish long-run or complete scientific qualification. |
| Full RZ findings | Continuous field/energy acceptance and the full meridional Newtonian FV stress, AMR work and stability qualification remain open. An owning homogeneous pre-step source has a complete independent interval reference; the distinct post-step source reached its work limit and remains incomplete. |
| CUDA JENS / 2D diagnostics | Explicit Cartesian CUDA Jeans has an engineering route; fresh local GPU acceptance and performance are pending. Automatic and curved CUDA selection retain their rejection conditions. |
| Validation storage | Actual Host mount and bounded output guards; passed raw files are processed and identity-checked before cleanup. Failures and unqualified evidence remain local. |
| Current engineering checks | All 367 Studio tests, lint, type checking and production build pass with the original waiting and drift checks. The current default tooling suite passes all 665 tests; five affected original CPU owners pass. These results do not replace the full current CPU/CUDA scientific campaign. |

Incomplete intended-release functions must be completed and validated before release. Changing release scope requires an explicit project decision. `main` and release tags are not changed by this integration. Raw HDF5/checkpoint/log data stay local; only bounded reviewed summaries are versioned.

[Historical checkpoint reports](docs/archive/README.md) retain exact earlier scopes and limitations. Their counts and machine paths are not current launch instructions.
