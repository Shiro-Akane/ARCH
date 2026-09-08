# Source publication record

This existing record identifies the maintained source delivery and its selected
verification evidence. Update it in place when the delivered files change; it
does not represent a new simulation or a precompiled binary release.

## Contents

The [archive manifest](archive-manifest.json) lists the selected source and
evidence files with their content identities. The branch contains maintained
implementation, configuration, documentation, canonical inputs, generation
recipes, compact reference data, and selected final verification records.
Small baseline archives preserve the exact pre-maintenance source and reviewed
documentation; their original hashes and published locations are in the manifest.

Generated HDF5 checkpoints and plots, executables, object/static/shared libraries,
dependency installations, profiler databases and large raw traces are not part
of this source delivery. Necessary multi-megabyte JSON evidence summaries are
retained explicitly, rather than silently dropping them by a blanket size rule.
The existing Helmholtz table keeps its unchanged Git LFS pointer. Runtime
`*_log.dat` text logs use ordinary, byte-preserving Git storage instead of LFS.

## Evidence and source identity

The [scientific acceptance](../final-acceptance-20260907/release-73a9cf50/README.md)
retains its recorded source identity and 41 required passes. The
[maintenance review](../maintenance-freeze-20260908/README.md) records the current
ARCH checkpoint reader, unified factory interfaces, test/tool organization and
actual local verification. The narrow dispatch cleanup preserves the active
registry mappings and numerical implementations. The optimized incremental CUDA
build, all 289 Python controls, 98 Release tests, ten development-smoke lanes
and all four normal/instrumented restart suites pass.

The current runtime campaign records source
`abd02d1eab0887e366ebf24506882efb1a49166e2607ff47ba1a89937107cedf`;
its final source, binary and test-artifact identity checks pass. Independent
scientific measurements retain their original source identity. The
[matched AMR timing](../maintenance-freeze-20260908/README.md#matched-local-amr-timing)
also passes its field, conservation, workload and identity checks. CPU is faster
at both measured sizes; the owner accepts integration as a functionally
equivalent CPU/CUDA version with that result disclosed.

The subsequent [CMake organization review](../maintenance-freeze-20260908/cmake-review.json)
qualifies the build-file changes separately: the effective CUDA build graph,
compile/link commands and generated contents are unchanged, and targeted host
builds and checks pass. This is not another full NVCC build or runtime campaign.
The source inventory records the new build-module identity while retaining the
executed identities of the scientific, runtime and timing records.

The configuration-output review in that same CMake record covers the quieter
default dependency messages and the explicit verbose option. It separates
first-configuration metadata differences from the settled CPU/CUDA build-graph
comparison, and records the actual diagnostics controls and short host checks.
This display-layer change does not alter compiler options or qualify a new
scientific run. Repository delivery also includes the security policy,
code-owner declaration and secret-scanner configuration; local scan payloads
and private audit logs remain outside the source archive.

Source delivery includes the capacity reviewer's allocation-lifetime and
process-identity self-test scripts beside its recipe. These two small inputs
are required by the acceptance index's `--self-test`; their inclusion does not
add the surrounding raw diagnostics, profiler outputs or HDF5 data.
The earlier local compile-concurrency reference also includes the small
`replay.py` named in its reproduction command. Its output arguments are checked
before redirecting compiler products into a temporary directory; no new heavy
compile measurement is claimed by including the recipe.

The archive manifest observes the current source and selected evidence directly.
It must include the intentional interface, fixture and documentation changes;
the earlier path-only surviving-source comparison does not establish equivalence
for this cleanup. Recorded scientific inputs, budgets and measurements are not
renamed to match a later Git commit.

The selected source and reviewed technical results are ready for the
owner-authorized fast-forward integration. Final local-link and inventory
checks guard the selected commit. Git refs and push receipts establish the
actual integration state; this record does not claim that a push has occurred.

## Updating effective validation records

Use the existing module inputs, runners and declared report locations. After a
successful rerun, replace that module's effective report and summary together,
recording the actual source/build identities and measured data. Do not create
parallel versioned result sets or copy a previous pass into a new identity.
Independent reference fixtures and required failure/protocol controls retain
their explicit test ownership; they are not additional current success reports.

A source checkout excludes original HDF5 products, locally built programs and
large profiler traces. Reproduction therefore requires the documented build and
dependencies. The selected evidence states the original observations and scope;
absence of an excluded runtime artifact is not itself a numerical failure.

Third-party provenance remains in [THIRD_PARTY_NOTICES.md](../../../../THIRD_PARTY_NOTICES.md).
The pending Timmes contact/redistribution confirmation is unchanged by this source
archive. Optional CUDA/provider binaries are not bundled.
