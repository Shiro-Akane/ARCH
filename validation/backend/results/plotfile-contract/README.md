# Plotfile candidate reader verification

2026-10-03. Source baseline: `35c5b7b114069621901386bfc4bc2a656e65af06`.
The [processed summary](summary.json) pins the final probe/test content and the
unchanged production writer. The [joint guide](../../../../docs/development/PlotfileValidationContract.zh-CN.md)
defines the candidate adapter and subsequent writer/query/Viewer review.

The production writer, physics, configuration API, checkpoint and Studio are
unchanged. This record qualifies the listed developer reader checks, not a
released Plotfile format or a completed Viewer. No new simulation or CUDA build
was performed.

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

Remaining production acceptance covers actual close/publish/failure propagation,
identity binding, complete AMR spatial ownership and Viewer I/O/memory/LOD/cache
behavior. Curvilinear geometry, 3D candidates and optional external bridges have
separate exits. HDF5 paths and UI layout remain candidate choices.

## DeepSeek assistance

Three bounded logical tasks used two isolated CLI slots with rolling refill:
probe implementation, analytic counterexample tests and a tool-free semantic
check. Each received its sole permitted repair. All six deliveries were rejected:
the first test draft had reproducible contract errors, three CLI calls timed out
with partial artifacts, and both high-effort API calls exhausted the 2,048-token
output cap before returning a structured result. Each used 2,048 reasoning tokens
and zero visible tokens; this is a manager-budget delivery limit, not a scientific
failure. No scientific finding from the semantic worker was accepted.

Codex consolidated the partial ideas, corrected index/measure/layout and input
handling, then independently ran the checks above. Parallel time included real
output/checkpoint inspection and handoff preparation; no explicit blocking wait
for a worker was used. Additional manager rework was required. The timing record
includes actual delayed notices and review intervals; they are not active-token
cost measurements or evidence of a speedup. One notice was recorded after an
earlier direct status inspection: its actual first-detection latency is unknown,
and the later notice event is not used as a reaction measurement or backfilled.

Cumulative worker wall time was 1,131.47 s, including overlap. Reported usage for
three calls gives a known cost subtotal of USD 0.0431–0.0862; total cost is unknown
because timed-out calls did not report usage, and manager cost was not measured.
Keep assistance bounded but reduce implementation granularity and state stored
indexes/output keys explicitly. A further identical short-cap API retry is not
recommended. Subsequent user-v7 scheduling uses a manager-owned regular-high
budget of 16,384 total output tokens, with final summary length controlled
separately; this does not reopen exhausted repair allowances. This single batch
does not establish general delegation efficiency.

Only scripts, semantic guidance, hashes and processed diagnostics are committed.
Raw H5/checkpoints/full arrays, task packets and worker logs stay local.
