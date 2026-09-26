# Contributor guide and working records

Start with [implementation ownership](ImplementationOwnership.md) before changing
shared mathematics, backend storage, or execution paths. The map identifies the
single maintained implementation and its CPU/CUDA consumers.

The [compute optimization plan (Chinese)](ComputeOptimizationPlan.zh-CN.md)
continues from the v1.2.0 main baseline on `compute/optim`: shared CPU/CUDA
mathematics and execution costs first. O6 requires the registered Cellular CPU
completion times to reach at most 10 times FLASH, with explicit resource,
scientific-error and generality checks; this is a target, not a measured result.
O7 cylindrical axisymmetry and O8 user boundary interfaces preserve that gate.
O9 rechecks the final version before long-duration validation; it is not the first
performance acceptance stage. Nonuniform grid design remains a later discussion. The
[current diagnosis (Chinese)](../../validation/gravity/flash/CurrentDiagnosis.zh-CN.md)
records the earlier module comparison and focused FLASH parameter evidence;
these plans do not expand the currently accepted capability matrix.

The [ARCH–FLASH comparison and optimization plan (Chinese)](FlashComparisonOptimizationPlan.zh-CN.md)
defines reusable cross-code models, separates common physics from ARCH's fuller
coupling, and records the EOS recovery-contract audit and staged optimization
gates. The [O5 evidence](../../validation/gravity/flash/O5OptimizationReport.zh-CN.md)
reports verified results, the FLASH face-EOS control, differences in thermal
coupling, and the remaining sign-off gaps. User-facing combination boundaries
live in the [Reference](../Reference.md#combining-methods-and-physics); registration
is not acceptance of every physical or numerical permutation.

For the planned self-gravity work, use the
[self-gravity, maintainability and GUI coordination plan (Chinese)](SelfGravityImplementationPlan.zh-CN.md)
to track module boundaries, stage contracts, decisions, GUI/Core integration
order and validation gates. The [curvilinear gravity plan (Chinese)](CurvilinearGravityPlan.zh-CN.md)
defines the P8–P13 extension, shared source mathematics, file consolidation
and staged geometry validation; it does not expand current supported capabilities.
The [P1 handoff (Chinese)](SelfGravityP1Handoff.zh-CN.md) records the integrated
GUI Core baseline, implemented boundaries and scoped CPU/CUDA evidence.
The [low-density and near-vacuum robustness plan (Chinese)](LowDensityRobustnessPlan.zh-CN.md)
defines the numerical repairs, reuse of existing physical controls, internal
safeguards, conservation diagnostics and CPU/CUDA validation required before
the formal P2 baseline. P1.5 adds no physical degrees of freedom or algorithm
micro-tuning controls; three already effective time-step keys are registered
in the standard catalogue. Advanced visibility belongs to the GUI. The
[parameter and legacy EOS retirement audit (Chinese)](ParameterRetirementAudit.zh-CN.md)
accounts for all 90 standard keys, identifies inactive settings and superseded EOS
paths, and defines their removal without legacy compatibility. Retained physics
keeps its scientific acceptance requirements. Implementation and current test
evidence are tracked in the [P1.5 report (Chinese)](P1_5ImplementationReport.zh-CN.md);
this numerical change is separate from P1's behavior-preserving refactor.
Its [main maintainability audit (Chinese)](MainMaintainabilityAudit.zh-CN.md)
records file-size reviews, source-math exemptions, dependency findings and
directory proposals. Size thresholds trigger review rather than mandatory
splitting or merging; keep related responsibilities together and preserve
supported behavior. The explicitly authorized retirements in the newer parameter
audit supersede earlier compatibility-preservation recommendations.
The [P3/P4 record](P3P4CompositeGravity.zh-CN.md) describes the CPU periodic
composite solver, coupling, accepted scope, evidence and remaining P5/P6 work.

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

P1.5 implementation and validation progress is tracked in
[the implementation report (Chinese)](P1_5ImplementationReport.zh-CN.md).
