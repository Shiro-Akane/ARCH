# CUDA performance and verification results

This report summarizes measured CPU/CUDA performance and numerical checks.
The original reports are preserved at
`3dab4e25f9877b6eb2112bbf59a8264618e8862a`. Each measurement remains associated
with the source, executable and hardware identified in its report. Start with
the timings below to choose a backend; use the linked records to inspect inputs,
individual samples and error checks.

## Accepted campaign results

The formal microphysics matrix contains **11 groups, 1,188 runs and 1,155
comparisons**, all reported passed. Burning and coupled cases use aprox13,
the real Helmholtz EOS and the registered dense solver; diffusion-only cases
use the ideal-gas EOS. The server was an H100-20C 20 GiB vGPU with a Xeon
Gold 6338 VM. CPU comparisons use the fastest measured thread configuration,
not an assumed single-thread reference.

### Measurement protocol

- The microphysics campaign uses 8, 32 and 128 **initial mesh blocks**, one
  warm-up and five alternating formal samples per configuration. The CPU
  reference is the fastest median among 1, 8 and 16 threads.
- The server exposes 32 vCPUs; neither dedicated physical CPU cores nor a full
  dedicated H100 are claimed. The GPU allocation is the 20 GiB H100-20C vGPU.
- End-to-end time runs from process startup through exit, including
  initialization, initial/final IO and the original trace. Post-run numerical
  qualification is excluded. These are not kernel-only, steady-state or cold
  operating-system-cache measurements.
- Network, EOS, integration methods, physical endpoints, output, strict
  floating-point settings and scientific tolerances retain the campaign's
  registered definitions. Numerical/workload acceptance does not imply that
  every cell made identical ODE attempt/rejection decisions on both backends.

### End-to-end results at 128 initial blocks

Times are medians in seconds. Speedup is `CPU / CUDA`; a value below 1 would
favor CPU. "Coupled" includes Hydro, burning, species/thermal/viscous diffusion and
dynamic AMR. The [backend guide](../../../../docs/CudaBackendStatus.md#choosing-a-backend-for-performance)
also shows all three mesh sizes.

| Workload | CUDA seconds | Fastest CPU seconds | CPU threads | Speedup |
| --- | ---: | ---: | ---: | ---: |
| Diffusion, RKL1 | 7.476759 | 7.503789 | 1 | 1.003615× |
| Diffusion, RKL2 | 9.695857 | 13.062333 | 1 | 1.347208× |
| Burning, BE_NR | 4.351336 | 8.608762 | 16 | 1.978418× |
| Burning, BD | 4.426753 | 11.563926 | 16 | 2.612282× |
| Burning, ROS4 | 4.148480 | 8.286513 | 16 | 1.997481× |
| Coupled, BE_NR + RKL1 | 83.352433 | 239.682178 | 16 | 2.875527× |
| Coupled, BE_NR + RKL2 | 86.306200 | 345.126534 | 16 | 3.998861× |
| Coupled, BD + RKL1 | 80.908782 | 317.463746 | 16 | 3.923724× |
| Coupled, BD + RKL2 | 83.346980 | 423.554466 | 16 | **5.081821×** |
| Coupled, ROS4 + RKL1 | 68.510012 | 252.177793 | 16 | 3.680890× |
| Coupled, ROS4 + RKL2 | 71.031615 | 355.661882 | 16 | **5.007093×** |

RKL1 diffusion is effectively at parity; its 0.36% difference is not advertised
as a meaningful speedup. The highest result, BD + RKL2, grows from 1.460× at
8 blocks through 2.856× at 32 blocks to 5.082× at 128 blocks. At 128 blocks,
CPU16 samples span 421.579–424.796 seconds and CUDA samples span 82.854–86.634
seconds. All five samples are retained. The workload reached `t=1e-10` with
706 macro-steps, 158 final leaf blocks and 120 topology changes. Its maximum
source-aware energy error was 8.2981e-14, against the unchanged 1e-12 budget.
See the [original BD + RKL2 report](https://github.com/Shiro-Akane/ARCH/blob/3dab4e25f9877b6eb2112bbf59a8264618e8862a/validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914/coupled-bd-rkl2-summary.zh-CN.md)
for dispersion, other field budgets and sample identities.

The separate Hydro/AMR campaign uses 2D Sedov, HLLC/PPM/RK3, CFL 0.4, maximum
level 2, regrid interval 2 and final time 0.02, with burning and diffusion off.
It scanned 1/2/4/8/16 CPU threads; CPU16 was fastest at both sizes. Its 120
runs include 100 formal samples and 20 warm-ups, with 140 within-thread and
16 cross-thread comparisons.

| Initial block layout | CPU16 seconds | CUDA seconds | Speedup |
| --- | ---: | ---: | ---: |
| 4×4 | 12.137696 | 7.642616 | 1.588160× |
| 8×8 | 61.703301 | 37.010442 | 1.667186× |

These are combined Hydro/AMR results, not isolated AMR acceleration. The original
[Hydro/AMR timing summary](https://github.com/Shiro-Akane/ARCH/blob/3dab4e25f9877b6eb2112bbf59a8264618e8862a/validation/backend/results/hpc-cuda-optimization/S4/timing-20260913/README.md)
retains the thread scan and paired host-thread settings.

### Interpreting the optimization

| Workload | Reported end-to-end GPU benefit | Scope |
| --- | --- | --- |
| Sedov with dynamic AMR | 1.588 / 1.667 times the fastest measured CPU throughput | Two sizes; Hydro and AMR together |
| Built-in burn | 1.98 / 2.61 / 2.00 times CPU throughput | 128 blocks; BE_NR / BD / ROS4 |
| Diffusion | RKL1 approximately equal; RKL2 about 1.35 times CPU throughput | 128 blocks |
| Hydro, burn, full transport and dynamic AMR | 2.88–5.08 times CPU throughput | Six ODE/transport combinations at 128 blocks |

Small workloads may still favor CPU. These are workload and hardware results,
not a universal speedup or the isolated benefit of an individual optimization.

The approximately 5× headline compares GPU with CPU. Against the earlier
already-batched GPU implementation with the same mathematical fixes, the last
workspace-reuse changes reduced 128-block time by about 5.54%/4.01% for RKL1/RKL2,
0.26%–1.12% for the three burning methods and 0.18%–2.14% for the six coupled
methods. These small differences are not independently claimed to be
statistically significant. Numerically failed historical implementations are
not used as performance references. Details are in the
[original all-module summary](https://github.com/Shiro-Akane/ARCH/blob/3dab4e25f9877b6eb2112bbf59a8264618e8862a/validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914/all-modules-summary.zh-CN.md).

The production 150/200-species route has Helmholtz correctness comparisons and
full-application timing. Its tested small applications remain approximately
5.0–10.3 times as long on GPU as on CPU8, or about 0.10–0.20× speedup. This is
negative acceleration, despite passing numerical comparisons. Further large-network performance work
is separate from the accepted built-in-network campaign. Experimental native
wave/window providers were never registered as production backends.

## Original reports and preserved experiments

<details>
<summary>Expand original benchmark reports, reproducibility records and archived experiments</summary>

The complete source snapshot, raw text, recipes, failure records and experiment
code remain available at immutable Git paths:

- [Delivery summary and large-network boundaries](https://github.com/Shiro-Akane/ARCH/blob/3dab4e25f9877b6eb2112bbf59a8264618e8862a/docs/development/CudaStageDelivery-20260918.zh-CN.md).
- [Hydro/AMR timing and comparisons](https://github.com/Shiro-Akane/ARCH/tree/3dab4e25f9877b6eb2112bbf59a8264618e8862a/validation/backend/results/hpc-cuda-optimization/S4).
- [Formal microphysics campaign](https://github.com/Shiro-Akane/ARCH/tree/3dab4e25f9877b6eb2112bbf59a8264618e8862a/validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914).
- [Experimental sparse providers](https://github.com/Shiro-Akane/ARCH/tree/3dab4e25f9877b6eb2112bbf59a8264618e8862a/validation/network/native-wave-candidate).
- [Twelve window-capacity checks](https://github.com/Shiro-Akane/ARCH/tree/3dab4e25f9877b6eb2112bbf59a8264618e8862a/validation/network/results/native-wave-20260917/window-capacity-v1).

The experimental capacity check reports 24 trajectories / 96 macro-step pairs,
with maximum field error about 3.199e-14 against its unchanged 2e-10 budget.
Long trajectories, larger multi-page windows and production Helmholtz paired
performance remain future work for that candidate, not missing tests of a
promoted implementation.

Large binary/HDF5 packages have their original manifests and storage receipts;
the Git text projection does not claim to contain every binary package.
Local integration preparation also retained a complete source archive and a
file-by-file inventory before moving historical server operations and raw
experiments out of the maintained tree.

This organization reduces the checkout's active file set. It does not rewrite
Git history or claim that existing repository/LFS download sizes have shrunk.

</details>

## Current verification entry points

- [Runtime matrix](../../verify_runtime_matrix.py) delegates to existing AMR,
  checkpoint and backend validators.
- [Coupled microphysics](../../verify_microphysics_coupling.py) checks shared
  field budgets, source-aware balance, transport schedules and restart.
- [Integration record](../../../../docs/development/HpcCudaIntegration.md)
  records only checks performed on the curated tree.

The benchmark vGPU could not run debugger-based sanitizer checks. That
environment limitation is retained as a limitation of those records, not a
failed numerical test and not a sanitizer pass.

## Integration regression checks

The v1.1.0 cleanup removes an optional offline AMR data recorder and its dedicated
configuration, diagnostics and tests. The production refinement criteria,
conservative transfers, CUDA kernels and checkpoint IO are unchanged. The
rebuilt source passes these focused checks:

- 350 tooling tests and seven shared/CUDA AMR/configuration tests, without skips.
- 24 short CPU/CUDA runs in one, two and three dimensions: 12 exact dataset
  comparisons, six matching before/after regrid event sequences and six
  successful continuations from pre-cleanup checkpoints.
- BD/RKL2 with all transport: 12 AMR/restart routes and nine comparisons, with
  an active ENUC limiter witnessed on every route.
- Four complete, clean memcheck/racecheck reports for the AMR composition and
  regrid-transaction tests.

The rebuild and runtime checks completed without swap use or a resource-guard
stop. These checks validate the cleanup, not a new performance campaign. The
approximately 5× result above retains its original source and hardware scope.

<details>
<summary>Expand verification identities, reproduction inputs and baseline evidence</summary>

The [current integration record (JSON)](integration.json) records the tested
source fingerprint, binary hashes, short-case parameters and report hashes.
It replaces the previous summary in place; the
[source-pinned baseline record](https://github.com/Shiro-Akane/ARCH/blob/efbca9f075d208017cf3db8e514f2ad25aebdbca/validation/backend/results/hpc-cuda-optimization/integration.json)
preserves the earlier, broader integration and instrumentation results.
The `changes_committed: false` field describes the capture before publication,
not a later change to the tested code.

Complete before/after runs compare all checkpoint attributes and datasets.
Short interrupted continuations exclude only file indices affected by their
extra final output. The maintained coupled-restart checks retain strict
same-backend controller/output-history reproducibility and compare adaptive
cross-backend trajectories at a common physical endpoint. Numerical budgets
are unchanged. Detailed maintenance decisions belong to the
[integration record](../../../../docs/development/HpcCudaIntegration.md).

</details>
