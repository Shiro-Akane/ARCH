# Contributing to ARCH

The [release plan](../../ARCH_RELEASE_PLAN.md) is the sole current work index. Scoped CPU and CUDA qualification and the axisymmetric cylindrical implementation have accepted evidence; final distribution, hosted CI and human platform acceptance remain separate. Near-zero-momentum and SNIa qualification limits remain in that plan. A completed implementation or unit test does not establish an unrestricted scientific release.

Start from the [implementation ownership map](ImplementationOwnership.md) when changing code. Grid and curvilinear numerics own coordinates, measures and geometric terms; optional gravity owns its field, solver and energy work. Shared numerical bodies serve Host and Device. CUDA owners manage launches, storage and synchronization. Studio separates Electron, the Linux Host service, browser contracts and rendering.

## Find the maintained owner

| Task | Read or change here | Verification |
| --- | --- | --- |
| Build, dependencies and local resources | [Build guide](../guides/Build.md), [CMake](../../cmake/README.md) | Affected configure/target; record compiler, jobs, time and RAM |
| Configuration and public API | [Strict input contract](ConfigurationContractPlan.zh-CN.md), [API](../../src/api/README.md), [Reference](../Reference.md) | Existing parser, missing/invalid-input and API contracts |
| Mathematical or physical method | [Source map](../../src/README.md), [ownership](ImplementationOwnership.md), [physics](../physics/README.md) | Owning Host/Device leaf and independent scientific reference |
| AMR, execution or resources | [AMR](../../src/amr/README.md), [driver](../../src/driver/README.md), [CUDA](../../src/cuda/README.md) | Existing schedule, transaction, failure and resource checks |
| Files and continuation | [IO](../../src/io/README.md), [restart validation](../../validation/restart/README.md) | Publication failure, identity rejection and applicable fresh comparison |
| Studio | [Studio source boundaries](../../studio/README.md#source-and-evidence-layout), [tests](../../studio/tests/README.md) | Typed request/response, owned lifecycle, lint and typed build |
| Documentation and comments | [Writing guidance](CommentAndDocumentationStyle.md) | Current claims, local links and non-comment behavior equivalence |

Choose the owner by responsibility, data lifetime, actual callers and build entry. Keep mathematical algorithms shared; add backend code only for execution or demonstrated resource requirements. A new file should serve a coherent responsibility, rather than a desired file length. Geometry must still work independently when optional gravity is disabled.

## Check a change

The [test guide](../../tests/README.md) explains how to run engineering checks; [Validation](../../validation/README.md) explains scientific references, measured errors, accepted budgets and limits. Extend the existing assertion owner and keep independent oracles independent. Tooling, CPU, Device and Studio have distinct responsibilities. A lane marked `NOT_APPLICABLE` has no test result; it is not evidence that tests ran.

Keep inputs, numerical standards, physical definitions and failure handling intact. Use focused verification for the affected path; reuse accepted evidence only within its original identity and scope. Do not rerun historical long campaigns for prose or infer device qualification from Host tests. Build measurements freeze source/configuration first and distinguish cold, no-op and incremental work.

Comments explain workflow, units, mathematical conventions, non-obvious constants and resource lifetimes beside the implementation. They do not repeat repair history or invent physical reasons for work limits. Frozen scientific data and upstream fits retain their original attribution and coefficients. Pure comments still change source digests; old binary measurements retain their original identities.

## Documentation and evidence

Current usage belongs in the [guides](../guides/README.md), configuration/API contracts in their maintained owners, and source reasoning beside the implementation. Current results belong in each [Validation summary](../../validation/README.md); detailed receipts stay under its `results/` directory. Raw HDF5, checkpoints and full logs remain local.

[Historical development records](../histroy/README.md) explain past decisions and qualification limits. Their pending items are dated observations, not a second current execution plan. Fixed-path receipts and original failures retain their bytes and provenance during documentation cleanup. The [modernization closeout](../../ARCH_MODERNIZATION_PLAN.md) preserves the completed architecture scope.

## Reporting and review

Use the [reporting guide](../guides/Reporting.md) for a small reproducer, source identity, command, expected result and bounded logs. [CODEOWNERS](../../.github/CODEOWNERS) identifies the reviewer; implementation ownership is a separate map. [CI](../../.github/workflows/README.md) defines automated coverage, applicability and required-result handling.

Before publishing source or history, use the maintained [.gitleaks.toml](../../.gitleaks.toml) rules with Gitleaks. From the repository root:

```bash
gitleaks git --log-opts="--all --diff-merges=separate" --config .gitleaks.toml --redact .
gitleaks git --pre-commit --staged --config .gitleaks.toml --redact .
```

Inspect materialized assets and archives as well as Git differences; keep detailed scanner reports local. Hooks are local setup and supplement hosted checks. Source sharing and formal publication follow the current task authorization.
