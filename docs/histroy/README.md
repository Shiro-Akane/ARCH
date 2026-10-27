# Historical records

These records preserve earlier decisions, measurements, limitations and failed
attempts. Their pending items describe the time of writing. Current work belongs
to the [release qualification index](../releases/README.md); current instructions start in
the [guides](../guides/README.md), [developer guide](../development/README.md) and
[scientific validation summaries](../../validation/README.md).

Use the subject directories to trace a decision. Their original scientific prose,
commands and numerical criteria are retained; relative Markdown links were rebased
where a document moved; the relocation receipt also records a verified correction
to one obsolete owner link. Measurements keep their original source and input identity.
Original host-local artifact references describe the original experiment; they
are not portable download instructions or current setup requirements.

| Subject | Records and starting points |
| --- | --- |
| Build and CI | [Build investigations](build/); [Windows native W0–W3 qualification](build/WindowsNativeQualification-20261026.zh-CN.md); [final CPU/CUDA/Studio correspondence](build/WindowsNativeCudaQualification-20261027.md); cold, incremental and hosted observations remain distinct |
| Configuration | [Input and provenance investigations](configuration/); the maintained [configuration contract](../development/ConfigurationContractPlan.zh-CN.md) owns current rules |
| Scientific methods | [Model and numerical investigations](science/), [geometry review](CurvilinearMetricReview.md), [transport coefficients](DiffusionCoefficientAlignment.md) |
| Integration | [Compute requirements](integration/ComputeOptimizationPlan.zh-CN.md), [Core/Studio integration](integration/ComputeStudioReleasePlan-20261006.zh-CN.md), [execution history](compute-studio/ExecutionHistory.zh-CN.md) |
| Modernization | [Original repository map](modernization/ModernizationR1-RepositoryMap-20261012.zh-CN.md), [working-entry preflight](modernization/Preflight-20261012.zh-CN.md), [closed modernization plan](https://github.com/Shiro-Akane/ARCH/blob/fe8f1f708cec34e42fa436f8cd21cec9285407e1/ARCH_MODERNIZATION_PLAN.md) |
| Low density | [Implementation](low-density/ImplementationReport.zh-CN.md), [robustness design](low-density/RobustnessPlan.zh-CN.md), [parameter retirement](low-density/ParameterRetirementAudit.zh-CN.md) |
| Optimization | [Implementation and acceptance ledger](optimization/ImplementationAndAcceptancePlan.zh-CN.md) |
| Release preparation | [Plan through CPU/CUDA qualification](release/QualificationThroughCuda-20261019.md); the maintained plan contains current actions and standards |

## Studio

[Studio history](studio/) combines the earlier desktop, Host, UI and API archives.
Start with the [dated status](studio/Status-20261010.md),
[configuration handoff](studio/StudioConfigurationHandoff.zh-CN.md) or
[Core API notes](studio/core-api/). Historical branch names and intermediate
protocol discussions are provenance, not current setup requirements.
Use the maintained [Studio README](../../studio/README.md),
[user guide](../guides/Studio.md) and [API owner](../../src/api/README.md) for
present behavior. Intermediate GUI reports do not replace final platform acceptance.

## Fixed historical evidence

Some human notes remain under their original paths because immutable receipts
refer to them. The [fixed-evidence index](../development/archive/README.md)
identifies these exceptions, including the 107 old cylindrical implementation
notes. Frozen JSON, scientific results, original failures and historical source
snapshots retain their paths and bytes. The current
[documentation receipt](../../validation/backend/results/release-readiness/REL5DocumentationAndTestConsolidation-20261019.json)
records this migration separately from the prior cylindrical retirement map.
No redirect pages or second current execution plan are maintained.
