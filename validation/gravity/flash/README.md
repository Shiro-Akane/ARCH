# ARCH and FLASH performance comparison

[中文](README.zh-CN.md) · [Gravity validation](../README.md)

These measurements compare complete simulation costs for specified problems.
The reference is a locally extended FLASH 4.8 Cellular application, with its
input and binary identities recorded alongside ARCH. FLASH is optional:
ARCH builds, numerical tests and CI do not require it.

## Method and interpretation

The comparison uses the same problem and physical end time and records mesh histories,
active physics, field errors and state repairs. ARCH and FLASH differ in
hydrodynamic timestep rules, face EOS evaluation and burning treatment.
In these runs, ARCH uses two thermally coupled Strang burn half-steps and
complete face EOS queries; the FLASH reference uses a fixed-temperature burner
within each call and disables face EOS work. A runtime ratio therefore measures
the cost of these configured tasks, rather than equal-error efficiency.

CPU timings use an i7-10700 with eight physical cores. ARCH OpenMP8 and FLASH
MPI8 use the same cores; each reported value is the median of three complete
process runs, including initialization and output. CUDA uses an RTX 3060 Ti
and one host thread. CPU8 and CPU16 are measured separately, and GPU ratios
use the faster CPU median for each case. Formal timing runs have no concurrent
build, profiler or simulation.

## Measured CPU costs

These values identify ARCH source `51f982ab` and the binaries recorded in
[the measurement directory](results/o6-controls-20260927/final/). FLASH repeats
show initialization variability, so the comparison keeps its original reference
samples as well as the repeat measurements.

| Case | ARCH default (s) | FLASH repeat (s) | FLASH reference (s) | ARCH/reference |
| --- | ---: | ---: | ---: | ---: |
| Sod | 5.028 | 2.822 | 2.630 | 1.91 |
| Jeans 64² | 0.717 | 1.318 | 1.324 | 0.54 |
| Jeans 128² | 5.780 | 2.973 | 2.876 | 2.01 |
| Cellular A | 7.082 | 8.178 | 5.018 | 1.41 |
| Cellular B | 7.182 | 10.273 | 5.203 | 1.38 |
| Cellular without burning | 4.527 | 9.023 | 4.177 | 1.08 |
| Cellular 0.25 cm | 32.350 | 9.596 | 9.261 | 3.49 |


The stated cost target was at most twice the original FLASH reference.
Five cases meet it. Jeans 128² exceeds it by about 0.48%, and the 0.25 cm
Cellular case takes 3.49 times the reference cost. These two results remain
above that target.

Selecting the explicit Davis wave-speed option reduces the fine Cellular
CPU8 median to **24.176 s**, or **2.61 times** the reference, still above the
two-times cost target. Davis changes the
numerical method; its comparison against the default uses the following
budgets: relative mass/total-energy integral difference at most `1e-3`,
volume-weighted density/pressure/temperature L1 at most 1%, and major-isotope
integral difference at most 1%. The common endpoint, positivity, composition
normalization and zero-repair checks also pass. The largest temperature L1
was `4.89e-9` for the four tested Cellular cases.

The fine case performs 4,337,664 leaf-cell macro-steps in ARCH and 1,441,280 in
FLASH, with 258 and 103 macro-steps respectively. This approximately 3.01-fold
work-count difference helps interpret runtime, but a cell macro-step contains
different EOS and reaction work in the two codes.

## Measured CPU/CUDA costs

| Case | Faster CPU (threads), s | CUDA (s) | CPU/CUDA |
| --- | ---: | ---: | ---: |
| Cellular A | 7.082 (8) | 14.677 | 0.483× |
| Cellular B | 7.182 (8) | 15.164 | 0.474× |
| Cellular without burning | 4.527 (8) | 9.335 | 0.485× |
| Jeans 128² | 5.780 (8) | 7.810 | 0.740× |
| Cellular 0.25 cm | 31.051 (16) | 57.290 | 0.542× |
| SNIa 2D Cartesian AMR | 5.077 (8) | 3.072 | 1.653× |
| SNIa 3D cylindrical AMR | 37.178 (16) | 45.308 | 0.821× |
| Cellular 0.25 cm, Davis | 24.176 (8) | 44.558 | 0.543× |


Every listed GPU sample passes its actual-backend, endpoint, topology and field
checks. The tested 2D four-module AMR case gains **1.65×**; the other default
cases yield **0.47–0.82×**, favoring CPU at these sizes. Separate CPU regression comparisons against their recorded reference
retain byte-identical fields for the seven cost problems; the two coupled
problems have a largest normalized Linf of approximately `7.62e-16`. Those
coupled checks cover five steps, with active burning, thermal diffusion,
self-gravity and changing AMR topology.

The timings belong to their recorded source and geometry. In particular,
2D cylindrical polar-plane measurements do not describe the current native
axisymmetric `(r,z)` chart. See [native-chart validation](../README.md#native-cylindrical-meridian)
and [platform support](../../../docs/releases/PlatformSupport.md) for those
capabilities. The current Helmholtz stellar closure supplies thermal conduction.

## Reproduce a comparison

Use [run_comparison.py](run_comparison.py) with locally built CPU and CUDA
executables. Generate new CPU reference outputs before checking GPU fields:

```sh
python validation/gravity/flash/run_comparison.py \
  --routes arch --cases a b noburn jeans128 fine snia2d snia3d \
  --threads 8 --repeats 3 --prefix cpu8 --output output/comparison \
  --manifest output/comparison/cpu8.json
python validation/gravity/flash/run_comparison.py \
  --routes arch --cases a b noburn jeans128 fine snia2d snia3d \
  --threads 16 --repeats 3 --prefix cpu16 --output output/comparison \
  --manifest output/comparison/cpu16.json
python validation/gravity/flash/run_comparison.py \
  --routes cuda --cases a b noburn jeans128 fine snia2d snia3d \
  --repeats 3 --prefix cuda --output output/comparison \
  --manifest output/comparison/cuda.json
python validation/gravity/flash/run_comparison.py --report-only \
  --manifest output/comparison/cuda.json \
  --reference-manifest output/comparison/cpu8.json \
  --reference-manifest output/comparison/cpu16.json
```

Default executables are `build-cpu/bin/ARCH` and `build-cuda/bin/ARCH`; use
`--arch-cpu` and `--arch-cuda` for other paths. Choose thread counts and optional
`--affinity` / `--cuda-affinity` masks for the actual machine. The scripts use
NumPy and h5py. Each run needs a new output/prefix/manifest. Recorded manifests
identify output files on the original machine; they are provenance, not local
field references for a new run. FLASH additionally requires its prepared build,
matching MPI launcher and, for Cellular, `--helm-binary`.

## Data and numerical controls

- [Timing, field and work-count data](results/o6-controls-20260927/final/) identify the source, binary, inputs and individual samples behind the tables.
- [Control and derivative checks](results/o6-controls-20260927/checks/) include CPU/CUDA contracts and device instrumentation.
- [Subsequent CPU measurements](results/cpu-followup-20261003/README.md) record separate source identities for EOS reuse, table loading and multigrid scheduling.
- [Complete source-bound study](https://github.com/Shiro-Akane/ARCH/blob/v1.4.0/validation/gravity/flash/O6ControlsAcceptanceReport.zh-CN.md) preserves compiler comparisons, rejected approximations, exact budgets and additional samples.

Rejected face approximations are not runtime options. Production uses complete
face EOS queries and strict floating-point compilation. The relevant
[configuration reference](../../../docs/Reference.md) describes `hll_wave_speed`,
`eos_coulomb_mult` and `dt_max`.
