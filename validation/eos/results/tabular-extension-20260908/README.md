# Tabular EOS and generated NSE extension checks

This is a bounded extension record, not a replacement for the complete CUDA
physics/restart acceptance profile. The tested source is the `CUDA_complete_v1`
worktree based on `e799640be7f6cbad678ec0e36a09b96b48c7c572`, with code/input
scope SHA-256 `5fc6ae75af92af155113c4b552ebf9953afe01e20d524a9422a63bdd2a3dde8c`.
[identity.json](identity.json) records the post-execution source, configured
compiler/dependency/package inventories and actual artifact identities. This
observation is explicitly not a fabricated before/after freeze certificate.
The static runner separately verifies before/after binary, recipe and both
primary/component-table inputs. Git refs establish later publication.

## Completed checks

| Check | Result | Evidence |
| --- | --- | --- |
| Complete CPU Release + KLU CTest | 37/37, no skips; full application/test build | [JUnit](component-ctest.xml), [inventory](component-inventory.json), [log](component-ctest.log) |
| Tooling and architecture | 307 controls, no skips; shared-authority audit passes | [tooling](component-tooling-final.log), [audit](component-architecture-final.log) |
| Real generated NSE packages | `nse_light`, `nse_alpha`, registry resolution pass | [JUnit](component-generated.xml), [network physics scope](../../../network/README.md#generated-nse-extension-2026-09-08) |
| Focused CUDA | Four device tests and their host fixture: 5/5, no skips | [JUnit](component-cuda.xml), [log](component-cuda.log) |
| Completed strict 3D/4D owner memcheck | Each rank: 6 points, 23 fields, both inverses; zero errors/leaks | [memcheck](component-cuda-memcheck.log) |
| Real native HShen owner memcheck | Host release, move, query/error latch; zero errors/leaks | [memcheck](native-cuda-final-memcheck.log) |
| Original baryon source EOS | Each table: 174/200 source nodes and 46/50 interiors accepted, explicit exclusions | [log](baryon-real.log), [EOS2 samples](eos2.tab-samples.tsv), [EOS4 samples](eos4.tab-samples.tsv) |
| Raw EOS CPU static integration | Two 16-cell, three-step bitwise-static runs; eight startup negative controls | [evidence](static/evidence.json), [replay](replay_static.py) |

The original EOS2/EOS4 files and their separate license are recorded in the
[data notice](../../../../EOS_toolkit/tables/baryon/README.md). Neither processed
HShen source data nor a converted total table is distributed in this record.
The retained [earlier native sample](native-real-final.log) belongs to the
preceding source snapshot; its 400-point results are not relabeled as a new
full-domain qualification.

Manufactured controls separately check isolated component formulas, all
supported missing-component combinations, non-default source baryon mass,
complete-table independence from the Helm dependency, single-potential
derivatives, gauge/mask transport, exact multiple/flat roots and domain errors.
CUDA owners are queried after host release and move; invalid queries yield NaN
and preserve the existing sticky error latch. Maximum scaled CPU/device
difference in the completed-owner witness is `3.355e-15` (budget `2e-10`).

The real-table sample uses fixed budgets: nodal P/E `2e-8`, energy inverse
residual `2e-12`, resolved temperature `2e-8`, with predeclared coverage minima.
Nodal P/E error is at most `2.427e-14`; resolved inverse temperature error is at
most `2.902e-14`. Both tables reject 7 sampled nodes outside electron support,
19 nodal and 4 interior derivative patches. Source-level invalid coordinates
and full-grid stencil counts are in the [EOS summary](../../README.md).
Nodal fidelity and interpolant closure do not prove continuous nuclear-matter
interpolation accuracy. No tolerance, source reference constant or EOS-name
switch was adjusted to make these data pass.

The static checks preserve all eight native state fields and 24 plotted fields
exactly. Temperature recovery differs from the prescribed `1e10 K` by at most
`1.888e-15` relative; mass/charge, conserved species and energy closure errors
are zero in these uniform states. They do not qualify nonuniform evolution,
burning with nuclear-equilibrium tables, long trajectories or restart physics.

## Reproduce

Use a CPU Release build with tests and KLU; run the complete inventory using
the [normal CI commands](../../../../.github/workflows/README.md). Run the
explicit real-source sample after fetching the LFS inputs:

```bash
build-ci/cpu/arch_baryon_eos /tmp/new-baryon-sample \
  EOS_toolkit/tables/helmholtz/helm_table.dat \
  EOS_toolkit/tables/baryon/eos2.tab EOS_toolkit/tables/baryon/eos4.tab
python validation/eos/results/tabular-extension-20260908/replay_static.py \
  --arch build-ci/cpu/bin/ARCH --output-dir /tmp/new-baryon-static \
  --helm-table EOS_toolkit/tables/helmholtz/helm_table.dat \
  --table EOS_toolkit/tables/baryon/eos2.tab \
  --table EOS_toolkit/tables/baryon/eos4.tab \
  --completed-table build-ci/cpu/component-completion-test-data/partial-3d-00.h5
```

The Python replay needs Python 3.10+ with NumPy/h5py and empty output directories.
Run `tabular_component_completion` first to generate its optional manufactured
input. CUDA CTest `tabular_completion_device` declares that host fixture itself;
build both `arch_tabular_completion` and `arch_cuda_tabular_completion` first.
The device executable also accepts Helm, manufactured-3D and manufactured-4D
paths directly for Compute Sanitizer. Generated package recipes and registration
are documented in the [network summary](../../../network/README.md).

Build logs retain actual optimization and memory-guard observations. The new
focused CUDA build took 342.607 seconds; its final host-control-only increment
took 6.020 seconds. These are neither whole-backend cold builds nor runtime
performance measurements. No full new CUDA application campaign was run.

## Retained diagnostic attempts

[diagnostics](diagnostics/) retains the first static replay and its failures.
The first environment had an older Python incompatible with the shared tooling.
The first application replay passed both raw tables and their six guards, but
the manufactured guard input used the raw source's state, outside its smaller
domain. The final replay chooses an interior state from that manufactured
file's declared bounds so it reaches the intended coupling rejection. No
production EOS change or relaxed numerical budget was made for this correction.
Archived artifact paths preserve their original execution locations; the replay
generates new paths and recomputes all identities.
