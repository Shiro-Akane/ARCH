# HDF5 checkpoint and restart continuity

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is authoritative.

The results on this page refer to the scientific acceptance version identified
in the [central Validation index](../README.md); subsequent directory maintenance
and new-build checks are recorded separately in the
[maintenance record](../backend/results/maintenance-freeze-20260908/).

CPU and CUDA use one checkpoint format and reader. A checkpoint can continue
on either backend when the destination build supports its physics and required
data. The tests compare resumed runs with uninterrupted runs, including dynamic
refinement and burning. Overall acceptance is tracked in the [validation index](../README.md).

## What a checkpoint preserves

Format v4 stores AMR leaf topology, conserved fields, native mass fractions and the
`ENUC` refinement field. It also stores timestep-controller values, output
indices and the loop phase so that a resumed run does not repeat regridding or
output work already completed at that checkpoint. Native fractions are saved
alongside species densities, so restoring composition does not lose precision
through a rounded multiplication and division by density.

The file identifies the EOS, its table content or ideal-gas parameters, the
network and NSE selection, and the ordered species. The reader checks this
scientific identity before restoring the state. CUDA uploads the restored
fields through its normal backend boundary; it does not use another IO format.

Earlier v1/v2 files remain readable. They do not contain ENUC or the v3
scientific identity, and v1 also lacks some controller state. Missing ENUC
starts at zero, so these files cannot reproduce an ENUC-based refinement history
that was never stored. Version-3 files retain that state and scientific identity,
but reconstruct mass fractions from species densities. Use v4 checkpoints to
preserve the original evolved composition as well.

## Completed checks on the release candidate

The [smooth-advection record](../amr/results/restart-smooth-native-20260907/release-876/restart-validation-evidence.json)
and [burn/ENUC record](../amr/results/restart-burn-native-20260907/release-877/restart-validation-evidence.json)
each pass twelve executions and nine comparisons on the same candidate source,
executable and comparator. Each has two uninterrupted runs, two source runs
and eight resumed runs: CPU-to-CPU, CPU-to-CUDA, CUDA-to-CPU and CUDA-to-CUDA,
from both a step-2 post-regrid checkpoint and a step-3 terminal checkpoint.
All resumed runs reach step 4, retaining mixed AMR levels 0–1: seven leaves
for advection and eight for burning. The burn case uses aprox13 with the
Helmholtz EOS and BE_NR with DenseLU.

All comparisons include conserved fields, native `X`, `rhoX`, `ENUC`,
timestep-controller state and output history. The table includes the
uninterrupted CPU/CUDA comparison and all eight continuations:

| Case | Executions / comparisons | Max field error / field peak | Max ENUC error / ENUC peak | Max relative burn-limit error |
| --- | ---: | ---: | ---: | ---: |
| Smooth advection | 12 / 9 | 0 | 0 | 0 |
| aprox13 burning | 12 / 9 | 6.523e-13 | 3.985e-13 | 3.984e-13 |

Same-backend continuations have zero field difference from their uninterrupted
references. Cross-backend burn continuations have maximum field-peak-normalized
error `1.661e-13`. Topology, loop phase, time and controller checks pass on every
route. Intermediate resumes preserve output numbering; terminal resumes have
exactly one additional checkpoint and plot, as derived from the actual stopped
source's output history.

The burn/AMR [memcheck campaign](../amr/results/restart-burn-native-20260907/memcheck-905/restart-validation-evidence.json)
and [racecheck campaign](../amr/results/restart-burn-native-20260907/racecheck-907/restart-validation-evidence.json)
each repeat the full twelve executions and nine strict comparisons on the same
candidate. Each instruments six actual CUDA executions: the uninterrupted run,
the source run and four CUDA-destination restores. All six memcheck reports
contain zero errors and zero leaked bytes or allocations; all six racecheck
reports contain zero hazards, errors or warnings. The CPU lanes remain ordinary
references. Native composition, controller state, loop phase and source-derived
output offsets pass the unchanged checks. Their numerical maxima match the
ordinary burn record above, and same-backend continuations retain zero field
difference. These are complete restart instrumentation records.

### Strict continuity budgets

These reports use strict reproducibility comparisons, not fixed-physical-time
scientific comparisons. Step count and topology must match exactly. Time and
`dt_old` use the dedicated binary64 roundoff allowance: eight ULPs at the tested
step 4. For each field, the maximum absolute difference must be no larger than
`5e-12 + 5e-9 × S`, where `S` is that field's largest magnitude across both
checkpoints. The original ENUC budget is `5e-12 + 1e-3 × S`; the burn-limit
comparison retains its `1e-3` relative allowance and `5e-12` absolute floor.
Output offsets are checked against the saved source history, not chosen to
make a comparison pass. HDF5 contents are compared through the shared reader;
container allocation and metadata layout are not numerical state.

## Sustained native restoration

The [current-candidate sustained record](../amr/results/sustained-first-law-20260907/release-901/evidence.json)
passes twelve restore cycles in each of three burn chains: CPU, CUDA and
alternating backends. Each of the 36 source states is restored through both the
shared Host reader and the CUDA upload/download path before further evolution.
All 72 native-state comparisons preserve fields, native composition, time,
controller and output metadata exactly, with zero field difference.

The 24 same-backend forward-continuation comparisons also have zero field
difference. The alternating chain retains twelve local forward-step diagnostics;
these do not substitute for physical acceptance. Six resumed endpoints and an
uninterrupted CPU/CUDA comparison pass at the prescribed `t=1e-10`. Their largest
field-peak-normalized difference is `7.673e-13`, within the original field and
ENUC budgets above. Exact restoration, strict same-backend continuation and
fixed-time physical comparison remain distinct checks.

A smooth-advection chain additionally passes twelve alternating restores to
step 26, with zero field difference in all fourteen history and endpoint
comparisons. Longer hydro and diffusion regridding runs are summarized in the
[AMR record](../amr/README.md#sustained-regridding-and-continuation).

## Reproduce the dynamic-AMR checks

Use a testing-enabled CUDA build with `ARCH` and
`arch_cuda_single_level_validation`. Run from the repository root and choose
a new output directory:

~~~bash
python3 tools/validate_cuda_amr_restart.py \
  --arch build-cuda/bin/ARCH \
  --checkpoint-validator build-cuda/arch_cuda_single_level_validation \
  --source-root . --build-dir build-cuda \
  --problem SmoothAdvection --input validation/amr/inputs/smooth_amr80_l1.par \
  --output-root validation/amr/results/restart-smooth-new
~~~

For burning, use `--problem BurnGradient`,
`--input validation/amr/inputs/burn_enuc_amr.par` and a different output directory.
The runner constructs both uninterrupted and resumed lanes and checks the
source checkpoint's actual step and phase.

For the instrumented burn checks, append
`--cuda-sanitizer /path/to/compute-sanitizer --sanitizer-tool memcheck` to the
burn command. Repeat with `--sanitizer-tool racecheck` and another new output
directory. The same runner keeps all four backend directions, both source
phases and the original numerical budgets.

Interruption inside an external library call is not covered by these completed
continuation tests.

## Historical records

Earlier [smooth](../amr/results/restart-smooth-native-20260907/release-758/restart-validation-evidence.json)
and [burn/ENUC](../amr/results/restart-burn-native-20260907/release-757/restart-validation-evidence.json)
reports retain their original source and binary identities. The
[earlier sustained record](../amr/results/sustained-first-law-20260907/release-763/evidence.json)
retains its original restoration and fixed-time measurements separately from
the candidate record above.

[metrics.csv](metrics.csv) preserves the earlier CPU uniform-grid and format-
compatibility audit, not the candidate's cross-backend measurements. Its
[audit notes](results/pre-release-notes-20260907/README.md) contain the original
inputs, compatibility details and reproduction commands; current application
metrics are in the two candidate reports above.
