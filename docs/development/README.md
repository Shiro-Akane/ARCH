# Contributing to ARCH

Start with the [implementation ownership map](ImplementationOwnership.md). CPU and CUDA use shared numerical and physical implementations; backend code manages execution and storage. Studio separates its Electron window, native Host service and browser interface.

## Find the implementation

| Area | Entry point | Relevant checks |
| --- | --- | --- |
| Build and dependencies | [Build guide](../guides/Build.md), [CMake](../../cmake/README.md) | Configure, affected targets and incremental builds on the selected toolchain |
| Configuration and API | [Input contract](ConfigurationContractPlan.zh-CN.md), [API](../../src/api/README.md), [Reference](../Reference.md) | Missing/invalid input, schema and request/response tests |
| Physics and numerics | [Source map](../../src/README.md), [ownership](ImplementationOwnership.md), [physics](../physics/README.md) | Independent references, conservation, convergence and Host/Device checks |
| AMR and execution | [AMR](../../src/amr/README.md), [driver](../../src/driver/README.md), [CUDA](../../src/cuda/README.md) | Schedule, state transactions, failure handling and resource limits |
| Files and continuation | [I/O](../../src/io/README.md), [restart](../../validation/restart/README.md) | Atomic publication, identity rejection and continuation comparisons |
| Studio | [Source boundaries](../../studio/README.md#source-layout), [tests](../../studio/tests/README.md) | Typed contracts, process lifecycle, file operations and desktop behavior |
| Documentation | [Writing guide](CommentAndDocumentationStyle.md) | Accurate examples, translations, images and links |

## Verify a change

The [test guide](../../tests/README.md) lists engineering checks. [Validation](../../validation/README.md) provides scientific inputs, references, error budgets and results. Select checks that exercise the changed behavior and its failure paths. Mathematical changes require independent scientific references as well as backend comparisons; preserve the accepted tolerances and physical definitions.

Measure build or runtime optimizations using the same source, configuration, input and hardware. Distinguish cold, incremental and no-op builds. Record runtime variability and peak memory alongside the numerical result. The [CUDA standard](CudaReleaseStandard.md) describes backend acceptance, and the [CI guide](../../.github/workflows/README.md) describes hosted coverage.

## Maintain documentation

Usage belongs in [guides](../guides/README.md), public settings in the [Reference](../Reference.md), and implementation reasoning beside its owner. Keep reusable scientific results and their reproductions in [Validation](../../validation/README.md). Finished task plans, intermediate status reports and local debugging logs belong outside the distributed documentation.

Describe present behavior and explain the choices a reader needs to make. Update both languages when changing commands, supported configurations or parameter meanings.

## Review and reporting

Use the [reporting guide](../guides/Reporting.md) for a small reproducer, source revision, command, expected result and relevant logs. [CODEOWNERS](../../.github/CODEOWNERS) identifies reviewers. Submit changes through a pull request.

Before publishing source or history, scan it using the repository's [.gitleaks.toml](../../.gitleaks.toml):

```bash
gitleaks git --log-opts="--all --diff-merges=separate" --config .gitleaks.toml --redact .
gitleaks git --pre-commit --staged --config .gitleaks.toml --redact .
```

Keep credentials, raw research output and full local diagnostic logs out of commits and distribution archives.
