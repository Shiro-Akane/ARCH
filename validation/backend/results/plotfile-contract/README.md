# Plotfile candidate reader verification

2026-10-03. Source baseline: `35c5b7b114069621901386bfc4bc2a656e65af06`.
The [processed summary](summary.json) identifies the probe, tests and production
writer used in this candidate-format reader assessment. The active production
format is documented in the [HDF5 reference](../../../../docs/Reference.md#hdf5-and-restart-format).

This assessment covers developer reader checks against small analytic inputs
and existing output files. It did not modify or test production publication
and Viewer behavior; those belong to the [Studio guide](../../../../docs/guides/Studio.md)
and [platform coverage](../../../../docs/releases/PlatformSupport.md).

## Checks and evidence

| Check | Result and scope |
| --- | --- |
| Candidate reader | 13 methods, no failures/skips; tiny analytic Cartesian 1D/2D leaves, FP64 sentinels, alternate mapping, identities, units, index order, malformed/nonfinite/linked/truncated inputs and CLI codes |
| Native read budget | One-cell hyperslabs, including the selected block's level/key; read-only file bytes preserved; logical request bytes are separate from actual I/O/chunk decompression |
| Numerical extremes | Large coordinate origin with subtraction roundoff; `1e-300` density; tiny representable 1D extent; rejection of substituted measures for unrepresentable 2D area |
| Existing output | Sod: 4,096 cells/4 fields; 2D AMR: 3,584 cells/28 fields, levels 0/1. All exported fields and Cartesian centers preserved bitwise in local synthetic-extension copies; density/energy match paired checkpoints bitwise |
| Selected native queries | First and last stored block/cell in each real-output copy match original values bitwise; bounds derive from paired checkpoint block coordinates and the associated input, not from center interpolation |
| Legacy files | Original 1D/2D files and an existing 3D Cartesian output recognized structurally with explicit missing semantics; trusted identity requests fail rather than inventing a producer |
| Tooling suite | Work Python 3.12.14: 376 methods, no failures, 13 Linux-pidfd skips, 19.649 s; that Python build lacks `os.pidfd_open` |
| Existing Linux controls | System Python 3.12.3: 22/22, no skips, 2.118 s; covers all 13 skipped methods and 9 overlapping methods, without changing tests or thresholds |
| Architecture audit | Passed; no production mathematics/backend ownership changes |

The existing tooling entry collects the new checks; there is no new CI lane,
backend matrix, CTest target or FLASH/Studio/third-party prerequisite. The two
interpreter records are reported separately, not as one skip-free CI run.

The real files predate this reader work. They do not store complete producer
identity, so synthetic headers explicitly mark case/config/build/binary/EOS as
unknown. Their `complete` marker tests a reader branch only. It does not prove
that the current writer closes and atomically publishes extended files.

Curvilinear geometry, 3D candidate decoding, production close/publish/failure
propagation and Viewer I/O/LOD/cache behavior are outside this reader assessment.
The original HDF5/checkpoints and full arrays remain local; this directory contains
processed results and input identities.
