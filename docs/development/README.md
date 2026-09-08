# Contributor guide and working records

Start with [implementation ownership](ImplementationOwnership.md) before changing
shared mathematics, backend storage, or execution paths. The map identifies the
single maintained implementation and its CPU/CUDA consumers.

The *owner* of an implementation is the specific file or module where its core behavior is defined and maintained. A caller might supply data to this implementation or decide *how* it should execute, but it must never introduce a duplicate copy of the same formula. *Memory owners*, on the other hand, serve a different purpose: they allocate system resources and guarantee they remain alive until all consumers have finished using them.

When changing a module, identify its owner and callers, explain the inputs and
assumptions that must stay valid, then select the relevant tests. Update the
public description when behavior changes and retain a separate record of the
verification. The [comment and documentation guide](CommentAndDocumentationStyle.md)
describes how to explain this flow without turning source comments into a change log.

## Security and contributions

Report suspected vulnerabilities through the [security guide](../../SECURITY.md)
([中文](../../SECURITY.zh-CN.md)) before sharing details in a public issue or pull
request. Ordinary bug reports should include a minimal input, the tested commit
and the expected behavior. Remove credentials and private data from shared logs.

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

## Current maintenance

- [Current interface review](ImplementationOwnership.md#current-interface-and-compatibility-review): the complete ARCH checkpoint
  contract, removal of unused solver-selection members, retained API boundaries
  and the focused verification record.
- [Release review ledger](CudaReleaseStandard.md): review scope, decisions and
  the evidence used for acceptance. Individual measurements identify the source
  and build they tested; retain that association when making changes.
- [Optimized core-build reference](../../validation/backend/results/cold-core-first-law-20260907/release-909/README.md):
  the measured cold, no-op and incremental builds, with the two-heavy/four-total
  job configuration. This is a measured reference, not a universal optimum.
- [Tests](../../tests/README.md) and [tools](../../tools/README.md): choose focused
  checks by responsibility and reuse the existing execution/evidence helpers.
- [Validation](../../validation/README.md): the combined acceptance record and
  links to scientific, runtime, instrumentation and resource measurements.

## Historical investigations

These records retain their original observations and source identities. Their
interim conclusions do not replace the current validation index.

- [Earlier compile concurrency experiment](../../validation/backend/results/local-build-reference-20260907/README.md):
  heavy-pool comparisons and memory-pressure observations preceding the complete
  core-build measurement above.
- [Earlier refactor experiments](CudaRefactorSmoke.md): historical smoke/build
  observations, retained for reproducibility rather than universal promises.
- [Archived backend evidence](CudaBackendEvidence.md)
  ([Chinese](CudaBackendEvidence.zh-CN.md)): the previous detailed backend
  reports, including hardware observations and historical qualification claims.
  These are preserved evidence, not the current acceptance decision.

User documentation lives in the project README, [API reference](../Reference.md)
and [backend capabilities](../CudaBackendStatus.md). Quantitative evidence uses
the existing [validation tree](../../validation/README.md): curated module
summaries link to detailed results, input identities and hardware metadata.
Keep reusable helpers in their owning module and run-specific evidence under
`validation/**/results/`. Link the existing user contract rather than copying a
second feature or acceptance checklist into a working note.
