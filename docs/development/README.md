# Contributor guide and working records

The [integration/release closure plan](ComputeStudioReleasePlan-20261006.zh-CN.md) governs the `compute/optim` merge of checkpoint `b7cb8b69`, compatibility repairs and completion criteria. Its [integration report](ComputeStudioIntegrationReport-20261006.zh-CN.md) separates current tests from historical/private evidence. The [earlier consolidation route](ComputeOptimConsolidation-20261006.zh-CN.md) preserves branch cleanup provenance; dedicated GNN work remains separate.

The [GUI branch retirement record](GuiBranchRetirement-20261006.zh-CN.md) makes `origin/compute/optim` the shared baseline for new short-lived Studio/Host tasks, preserves old contract documents and records the recovery bundle. Completed GUI branches are not parallel development baselines.

Start with [implementation ownership](ImplementationOwnership.md) before changing shared mathematics, backend storage or execution. The map identifies the maintained owner and its CPU/CUDA consumers. The [acceptance checklist](CudaReleaseStandard.md) describes validation and publication requirements; [comment and documentation style](CommentAndDocumentationStyle.md) covers source comments and reader-facing documentation.

The [self-gravity implementation plan](SelfGravityImplementationPlan.zh-CN.md) and [curvilinear gravity plan](CurvilinearGravityPlan.zh-CN.md) remain active references for field ownership, AMR coupling and geometry. GUI and Core contracts remain with the [API module](../../src/api/README.md).

The [compute optimization plan](ComputeOptimizationPlan.zh-CN.md) defines the next configuration, Jeans-resolution, axisymmetric-geometry and boundary-interface work, followed by long-duration validation and CI coverage/runtime consolidation. The [execution-cost follow-up](../../validation/gravity/flash/results/cpu-followup-20261003/README.md) records exact-state reuse and scheduling changes, original scientific budgets and remaining performance targets. The [configuration contract plan](ConfigurationContractPlan.zh-CN.md) classifies every standard parameter by requiredness, permitted defaults and physical ownership. The [archived implementation ledger](archive/optimization/ImplementationAndAcceptancePlan.zh-CN.md) preserves the completed optimization campaign. The [FLASH comparison record](FlashComparisonOptimizationPlan.zh-CN.md) describes optional developer comparisons; ARCH build, test, CI and release requirements are independent of external simulation programs. The [validation index](../../validation/README.md) links measured results.

The [joint delivery entry point](StudioConfigurationHandoff.zh-CN.md) gives the collaborator a reading order, branch setup, configuration/API and Studio workflow, and evidence requirements. Current Linux usage is in the [Studio guide](../guides/Studio.md), with [tests](../../studio/tests/README.md) and [historical checkpoints](../../studio/docs/archive/README.md) kept separately. Native Windows adaptation is outside this delivery. The [Jeans, RZ and second-platform execution guide](JeansRZPlatformHandoff.zh-CN.md) extends that responsibility through O7.1–O7.5 implementation and the approved O9 subset on an i7-14700K/RTX 4070 Ti system. Maintainers approve scientific rules, references and budgets. Process raw outputs locally and upload only reviewed summaries, metrics, figures and bounded diagnostic extracts; HDF5, plotfiles, checkpoints and full arrays stay on the producing machine. The [current contract audit](../../validation/backend/results/studio-config-contract-audit-20261001/README.md) preserves the inspected baseline; it does not certify the unpushed Studio checkpoint or future contract.

The [Plotfile validation bridge](PlotfileValidationContract.zh-CN.md) supplies small
counterexamples, adjustable HDF path mappings and native-cell readback for the joint
Viewer work. It checks local reader consistency; the collaborator continues to own
production writer/query/Viewer delivery and its publication/failure acceptance.

The [historical archive](archive/README.md) preserves completed low-density/EOS migration, implementation decisions and earlier backend reviews. Current user-facing behavior is described by the [feature list](../Features.md) and [reference](../Reference.md).

The *owner* of an implementation is the specific file or module where its core behavior is defined and maintained. A caller might supply data to this implementation or decide *how* it should execute, but it must never introduce a duplicate copy of the same formula. *Memory owners*, on the other hand, serve a different purpose: they allocate system resources and guarantee they remain alive until all consumers have finished using them.

When changing a module, identify its owner and callers, explain the inputs and
assumptions that must stay valid, then select the relevant tests. Update the
public description when behavior changes and retain a separate record of the
verification. The [comment and documentation guide](CommentAndDocumentationStyle.md)
describes how to explain this flow without turning source comments into a change log.

## Reporting problems and contributing

Use Issues for build, runtime and numerical questions, with a small reproducer,
the tested commit and the expected behavior. Follow the
[research computing and reporting guide](../guides/Reporting.md)
([中文](../guides/Reporting.zh-CN.md)) when preparing inputs and logs for sharing.
Problems involving unintended file access, credentials or effects on other users'
jobs need private coordination; ordinary numerical discrepancies do not.

[CODEOWNERS](../../.github/CODEOWNERS) names the default reviewer; it is separate
from the implementation ownership map and does not itself require approval.
For changes to build tools or dependencies, describe what will execute and any
new access it needs. Run contributed code only in an environment appropriate
for its trust level, without exposing credentials. Follow the existing
[test guide](../../tests/README.md) and keep each contribution focused.

Before sharing a checkout, scan its history for accidentally committed credentials
with [Gitleaks](https://github.com/gitleaks/gitleaks). The maintained
[configuration](../../.gitleaks.toml) uses the scanner's standard rules without
blanket exclusions or a baseline of ignored findings. With Gitleaks installed,
run these commands from the repository root:

```bash
gitleaks git --log-opts="--all --diff-merges=separate" --config .gitleaks.toml --redact .
gitleaks git --pre-commit --staged --config .gitleaks.toml --redact .
```

The first command includes merge differences in the locally available history;
the second checks staged changes. Fetch the intended remote branches first.
Git-diff scanning does not replace checking materialized LFS assets and archive
contents. Keep detailed reports outside the published source and do not paste
credentials into an issue. Revoke an exposed credential before addressing its
history. Commit hooks are local setup: cloning the repository does not install
them, and they supplement rather than replace GitHub push protection.

## Maintained contributor references

- [RT/AMR data-correctness repair](RTAmrDataCorrectnessRepair.zh-CN.md): shared
  acceptance rules, RKL correction accounting, portable replay inputs and the
  current GNN collection-admission decision. This is a targeted development
  record, not an additional release-wide validation claim.
- [Current interface review](ImplementationOwnership.md#current-interface-and-compatibility-review): the complete ARCH checkpoint
  contract, removal of unused solver-selection members, retained API boundaries
  and the focused verification record.
- [Acceptance checklist](CudaReleaseStandard.md): implementation invariants,
  required checks, resource policy and publication boundaries.
- [Build guide](../guides/Build.md): configurable compilation and memory controls.
- [Tests](../../tests/README.md) and [tools](../../tools/README.md): choose focused
  checks by responsibility and reuse the existing execution/evidence helpers.
- [Validation](../../validation/README.md): the combined acceptance record and
  links to scientific, runtime, instrumentation and resource measurements.

<details>
<summary>Integration review and historical investigations — for contributors</summary>

- [Integration record](HpcCudaIntegration.md): exact source and binary identities,
  bounded implementation changes, local checks and publication scope.
- [Historical development archive](archive/README.md): intermediate backend,
  AMR, geometry and diffusion reviews, plus the superseded acceptance ledger.
- [Core-build measurement](../../validation/backend/results/cold-core-first-law-20260907/release-909/README.md):
  identified cold, no-op and incremental builds; the recorded concurrency is a
  machine-specific reference, not a universal optimum.

These records explain development decisions. User-facing results remain in the
validation summaries; an archived pending item is not necessarily a current defect.

</details>

User documentation lives in the project README, [API reference](../Reference.md)
and [backend capabilities](../CudaBackendStatus.md). Quantitative evidence uses
the existing [validation tree](../../validation/README.md): curated module
summaries link to detailed results, input identities and hardware metadata.
Keep reusable helpers in their owning module and run-specific evidence under
`validation/**/results/`. Link the existing user contract rather than copying a
second feature or acceptance checklist into a working note.
