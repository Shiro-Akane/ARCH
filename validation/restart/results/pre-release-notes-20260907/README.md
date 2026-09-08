# Preserved restart notes before public-page curation

Historical contributor record, retained on 2026-09-07 JST. See the [current restart guide](../../README.md) for current use and acceptance. This archive preserves its original tested scope and hardware record.

# HDF5 checkpoint and restart continuity

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

The current Release candidate passes dynamic-AMR restart in all four CPU/CUDA
directions for smooth advection and ENUC-driven burning. The retained v1/v2
audit below documents earlier format compatibility; current v3 application
results have their own inputs and artifact identities.

This record verifies that a run resumed from an intermediate checkpoint follows
the same trajectory as the uninterrupted run. Format v3 stores the conserved
AMR leaf state and `ENUC` together with `dt_old`, the burn limit carried into the
next macro step, and the loop phase needed to avoid repeating regrid or
step-based output work. It also records the resolved EOS identity, ideal-gas
gamma or EOS-table identity and digest, burn/network/NSE selection, and ordered
species metadata. The reader retains v1/v2 compatibility, but those legacy
formats have neither ENUC nor scientific provenance; v1 also lacks the
controller state required for exact trajectory continuity.

## Current v3 application checks

Both the [smooth-advection record](../../../amr/results/restart-smooth-shared-helm-20260907/restart-validation-evidence.json)
and the [burn/ENUC record](../../../amr/results/restart-burn-shared-helm-20260907/restart-validation-evidence.json)
contain twelve executions and nine comparisons. Each covers CPU-to-CPU,
CPU-to-CUDA, CUDA-to-CPU and CUDA-to-CUDA continuation from intermediate
post-regrid and terminal checkpoints. Topology, conserved fields, ENUC,
timestep-controller state and output history pass the declared checks.
The shared runner is `tools/validate_cuda_amr_restart.py`; the exact commands,
input hashes and tested binary identities are retained in those records.

## Historical audit environment

The audited working tree was based on
`affde827fcbf317382ed45372912b562652a71c5` plus the changes recorded here. It
used GCC 13.3.0, the CPU backend, Release flags
`-O3 -march=native -ffast-math -DNDEBUG`, and two OpenMP threads on an
Intel Core i7-10700 under x86_64 WSL2.

## Cases and acceptance

`uninterrupted.par` writes a checkpoint at step 25 of the 64-cell periodic
SmoothAdvection case. `resumed.par` continues that file to step 50. The second
pair performs the same split at step 10 of the aprox13, Helmholtz, BE_NR,
DenseLU one-zone burn and therefore also exercises all species arrays and the
burn timestep limit.

The final uninterrupted and resumed HDF5 files are compared as complete HDF5
objects. Acceptance requires zero differences in every conserved field,
species field, leaf level/logical coordinate, scalar attribute, output index,
and continued timestep. Both cases pass with zero logical differences. Binary
HDF5 container bytes are not an acceptance criterion because allocation and
metadata layout can differ while every HDF5 object is equal.

## Reproduce

~~~bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 4
export OMP_NUM_THREADS=2

./bin/ARCH SmoothAdvection validation/restart/inputs/uninterrupted.par
./bin/ARCH SmoothAdvection validation/restart/inputs/resumed.par
h5diff output/validation/restart/uninterrupted/RestartSmoothAdvection_chk_0002.h5 \
       output/validation/restart/resumed/RestartSmoothAdvection_chk_0002.h5

./bin/ARCH BurnOneZone validation/restart/inputs/burn_uninterrupted.par
./bin/ARCH BurnOneZone validation/restart/inputs/burn_resumed.par
h5diff output/validation/restart/burn_uninterrupted/RestartBurnOneZone_chk_0002.h5 \
       output/validation/restart/burn_resumed/RestartBurnOneZone_chk_0002.h5
~~~

An isolated audit probe (not retained as a repository test target) wrote and
read synthetic two-block, two-species v2 data, read a legacy v1
multidimensional file, and checked output-index and timestep-controller
restoration. A legacy v1 step-zero checkpoint was also advanced through one
real hydro step: it recomputed the CFL limit, emitted no duplicate initial
files, and used a finite controller fallback. Restarting an already-final v2
checkpoint is a no-op and emits no duplicate final files. Configuration now
rejects `restart = true` with an empty `restart_file` instead of silently
starting a fresh run. A scheduler probe with `plt_dt = chk_dt = 1e-12`
confirmed that `t = 0` does not produce a duplicate time-based output; a
continuous schedule and restart reconstruction both selected the identical
next value `0.60000000000000009` in the longer accumulation check.
These are retained historical v1/v2 CPU results. Current v3 runtime results are
listed separately above.

## Scope label

- **Verified historical CPU evidence:** HDF5 v1 multidimensional read
  compatibility; v1 step-zero continuation; v2 round trip and no-op restart;
  uniform-grid hydro continuity; species/burn continuity; metadata and output
  numbering; empty-path rejection.
- **Implemented and source/compile qualified:** checkpoint v3 writes ENUC and
  scientific provenance, validates that provenance on restore, and uses the
  same Host schema before uploading restored state to CUDA.
- **Current real-device checks passed:** uninterrupted-versus-split-run
  comparisons in all four backend directions, dynamic-AMR topology and
  `refine_var = ENUC` continuity. Legacy v1/v2 files initialize ENUC to zero and
  do not provide the same ENUC-based continuation contract.
- **Pending:** interruption during an external library call.
