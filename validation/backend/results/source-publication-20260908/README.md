# CUDA_complete_v1 source archive

This record accompanies the independently frozen source branch requested by the
project owner. It packages the reviewed worktree; it is not a new simulation,
another scientific qualification, or a precompiled binary release.

## Contents

The [archive manifest](archive-manifest.json) lists the selected source and
evidence files with their content identities. The branch contains maintained
implementation, configuration, documentation, canonical inputs, generation
recipes, compact reference data, and selected final verification records.
Small baseline archives preserve the exact pre-maintenance source and reviewed
documentation; their original hashes and published locations are in the manifest.

Generated HDF5 checkpoints and plots, executables, object/static/shared libraries,
dependency installations, profiler databases and large raw traces are not part
of this source delivery. They remain in the owner's local workspace; no local
results were deleted. Necessary multi-megabyte JSON evidence summaries are
retained explicitly, rather than silently dropping them by a blanket size rule.
The existing Helmholtz table keeps its unchanged Git LFS pointer. Runtime
`*_log.dat` text logs use ordinary, byte-preserving Git storage instead of LFS.

## Evidence and version identity

The [scientific acceptance](../final-acceptance-20260907/release-73a9cf50/README.md)
retains its original source73 identity. The subsequent
[maintenance freeze](../maintenance-freeze-20260908/README.md) records the path-only
source comparison, optimized rebuild, 98 passing Release tests and ten passing
development-smoke lanes. Neither record is rewritten to name this later commit.

The source/input fingerprint in those records describes an uncommitted worktree,
including tracked paths already deleted at that time. Staging the intended
deletions and creating a commit changes Git/source identity without changing the
surviving source bytes. The archive manifest therefore checks every surviving
source input against the maintenance comparison, separately from Git identity.

Old reports preserve their original absolute paths and hashes. A source checkout
does not contain the original HDF5 files, locally built programs or all raw traces
required to replay the complete historical identity audit. Reproduce the tests
with a fresh build and new result directories; do not treat missing excluded
artifacts as evidence that the recorded numerical run failed.

Third-party provenance remains in [THIRD_PARTY_NOTICES.md](../../../../THIRD_PARTY_NOTICES.md).
The pending Timmes contact/redistribution confirmation is unchanged by this source
archive. Optional CUDA/provider binaries are not bundled.
