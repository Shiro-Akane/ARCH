# Maintenance freeze review — 2026-09-08 JST

Chinese summary: [README.zh-CN.md](README.zh-CN.md).

This is a contributor and verification record for the owner's final readability
and maintainability pass. The governing scope is the
[release standard](../../../../docs/development/CudaReleaseStandard.md).
It is not another scientific acceptance campaign.

Status: complete; the maintained worktree is ready to freeze. The
[final identity inventory](freeze-review.json) identifies the reviewed source,
documentation and evidence. No commit or push was performed.

## Scope and review

The CUDA runtime now groups control, hydro, burn, AMR and diffusion by
responsibility. Thin built-in burn bindings live under `burn/routes`; public
runtime headers remain at their existing entry points. Other cohesive modules
use functional README indexes without unnecessary file subdivision. User guides,
implementation navigation and validation evidence have separate entry points.

The [44-path map](runtime-path-map.json) and
[independent comparison](relocation-review.json) account for all 452 existing
source inputs. Bodies and literal contents are unchanged after resolving include
relocations. Two ODE blocks received indentation cleanup; two tabular-EOS header
comments were corrected to describe the existing implementation. No mathematics,
physics, constants, thresholds, budgets, factory selection or inline policy was
changed. Architecture checks and all 95 corresponding regression controls pass.

The comparison also preserves all 280 compiler command records, 98 CTest
registrations, linker rules/settings and the heavy compilation pool after
normalizing the declared source and build paths. It does not claim byte-identical
Ninja metadata: the previous timing wrapper and debug-metadata fields differ.
The [review recipe](review_relocation.py) includes 19 positive and negative
controls and reuses the existing source-identity and comment-scanning helpers.

Inline annotations were reviewed in context. Small shared helpers and intentional
large CUDA instantiation boundaries retain their existing policies. The maintained
header graph has no detected include cycle. Potential further dependency trimming
is deferred: it is not needed to make this path-only candidate maintainable and
would broaden the verification scope. Similar-looking AMR slope helpers retain
their distinct exceptional-value behavior rather than being merged by name.

## Evidence continuity

The original scientific acceptance belongs to source
`73a9cf50bbd4405972160ecb1742da33f52666466b33d1fa8d5d1949cffed171`.
Its [delivery inventory](../final-acceptance-20260907/release-73a9cf50/)
is preserved with the original binaries, tests, budgets and identities. The
maintenance source is
`4f4220712d6f76386bc126a0ba32b0d09f45e058b9e1c5f02fc25c7935dc9282`;
the path change is recorded, not hidden or applied retrospectively to results.

[baseline.json](baseline.json) identifies the original source inventory and
recoverable archive. [document-baseline.json](document-baseline.json) preserves
the identities and archived versions of the previously reviewed documentation.
Source and document archives are local build artifacts; the JSON records retain
their identities. Historical scientific records and failed attempts are not
rewritten during navigation cleanup.

The [preservation review](preservation-review.json) verifies the original four
critical acceptance artifacts and all members of the source/document archives.
The [navigation review](document-navigation-review.json) checks 138 Markdown
files, 1,318 local links, 59 heading anchors and README coverage for 99 functional
directories. External websites were not fetched. Its point-in-time document
hashes precede the three declared completion-text updates; the final inventory
records their completed versions rather than rewriting that review.

## Build and runtime checks

The isolated build under `build/maintenance-freeze-20260908/core` retains the
accepted optimized Release configuration, representative generated networks,
real KLU/cuDSS providers, two heavy compilation jobs and four total jobs. Existing
dependency sources are reused read-only; previous accepted binaries are not
rebuilt. The usual memory guard permits bounded swap use and monitors pressure.

Configure, the CUDA compile-probe target and the default full build pass. The
[build record](build-review.json) retains commands, logs and resource observations.
The full build takes 2,698.485 seconds, including the test targets; compiler
caches were not cleared, so this is a maintenance rebuild rather than a new cold
core-build benchmark. The guard observes at least 3,555,656 KiB available RAM,
no swap growth and no protection stop.

The [new-binary execution record](runtime-review.json) passes all 98 configured
Release tests with zero failures and zero skips. All ten existing development
smoke lanes also pass: CPU/CUDA dynamic Cartesian AMR, cylindrical and spherical
AMR with diffusion, and checkpoint continuation. This smoke checks integration,
not independent scientific accuracy or restart field parity. Regression and
smoke use the new ARCH and its configured comparator; before/after identity
checks include source, build controls, test definitions and observed executables.
The [execution recipe](run_maintenance_checks.py) delegates to existing CTest,
smoke, logging and provenance facilities. Runtime checks finish in 296.099
seconds under the same guard, without swap growth or a protection stop.

The previous full scientific and sanitizer campaign is not repeated for this
non-algorithmic pass. Its unchanged mathematics and original tested identities
are connected to this worktree by the source/configuration review, not by
relabeling old runs. Publication and the owner's pending third-party
redistribution confirmation remain separate from technical workspace freeze.
