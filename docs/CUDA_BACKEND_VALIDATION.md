# CUDA backend validation and benchmarking

This document defines the evidence required before the CUDA backend is described
as physically equivalent to, or faster than, the CPU/OpenMP backend.  Kernel
checks, whole-application checks, and comparisons with another simulation code
answer different questions and must be reported separately.

## Existing evidence

### Evidence that can be retained

- The manually translated Timmes networks have been compared with their original
  Fortran implementations.  The reported worst relative errors for the RHS,
  Jacobian, and implicit LHS are below `1e-12` for iso7, aprox13, aprox19, and
  aprox21.
- The current translated aprox13 CPU and CUDA operators were compared at six
  thermodynamic states on the JAIST H100 with FMA contraction disabled.  The
  measured relative errors are `6.430015946161e-13` for the RHS,
  `5.218754100417e-13` for the complete Jacobian,
  `5.218924419082e-13` for the implicit LHS,
  `3.752199827760e-13` for the temperature column, and
  `5.218754100417e-13` for the temperature row.  All outputs were finite and
  the validation executable returned PASS.
- The other current networks now have the same H100 validation.  Their worst
  full-system relative errors over RHS/Jacobian/LHS/temperature terms are
  `3.322158932277e-15` (iso7), `9.263982754389e-14` (aprox19), and
  `2.320173521360e-13` (aprox21).  The aprox19/aprox21 cancellation
  columns have separately normalized diagnostics of `2.57e-12` and
  `6.61e-12`, while the complete Jacobian norms remain below `1e-12`; both
  values must remain visible in reports.
- A current aprox13 Backward-Euler/Newton/dense-LU CPU/GPU baseline passed seven
  stiff success states and one expected invalid-input state.  It measured
  `1.44e-21` composition error, zero temperature error, `1.71e-12` nuclear
  energy error, and `3.31e-20` residual error.  Its one-thread-per-cell kernel
  uses 255 registers, about 10 KB stack per thread, and only 12.5% theoretical
  occupancy, so it is a correctness baseline rather than the production ODE
  design.
- The validation-only aprox13 batch ABI passed its H100 SoA/stride/status test,
  including asynchronous launch semantics, failure rollback, padding safety,
  and `max |sum(X)-1|=3.33e-16`.  It uses fixed `cv` and fixed substeps and is
  therefore not a production Helmholtz/NSE launcher.
- The aprox13 RHS+complete-Jacobian+temperature+LHS microbenchmark reached
  `66.982x` kernel-only and `47.239x` including one batch H2D/kernel/D2H at
  262,144 cells versus the 16-thread CPU path.  This is a network-operator
  microbenchmark, not an ODE, NSE, hydro, or two-dimensional speedup.
- The one-warp-per-cell aprox13 Newton/LU prototype preserved the one-thread
  GPU result exactly and reduced registers from 255 to 128, but its measured
  throughput was only `0.156x` of the one-thread kernel.  It is deliberately
  not registered for production.
- The OpenMP high-temperature smoke tests produced bitwise-identical HDF5 data
  with one and sixteen threads.  This is a useful determinism regression test,
  but it is not a CUDA performance measurement.

The detailed historical records are in `archgpu/TIMMES_CPP_VALIDATION.md` and
the network technical note.  When quoting a result, retain the executable,
commit, build flags, parameter file, and output checksum that produced it.

### Evidence that must not be used as a current speedup claim

- The standalone `burnbench` 6.8x result and the early `archgpu` 15x/102x
  results were obtained from legacy or separate experimental paths.  Their own
  notes identify an aprox19 species/ODE-size mismatch, a temperature-index
  mismatch, and incomplete nuclear-energy coupling.  They are valuable
  optimization experiments, not measurements of the current four-network ARCH
  backend.
- The old `cuda-v1` path is a constant-gamma, ideal-gas hydrodynamics prototype.
  It does not provide the same Helmholtz EOS, burning, NSE, species transport,
  or boundary behavior as the CPU Cellular path.  Comparing their wall times
  would compare different physics.
- Existing two-dimensional translated-network directories marked
  `INVALID_RUN.txt` contain unresolved or capped states.  They must never be
  used as physical validation images or performance acceptance runs.

Until a single revision runs the same initial state, EOS, network, ODE, NSE,
hydrodynamics, boundaries, and output schedule on both backends, the project has
no defensible end-to-end CPU-to-CUDA speedup number.

## Runtime selection and build modes

A CUDA-capable executable may contain both backends and select `cpu`, `cuda`, or
`auto` from the parameter file.  A CPU-only build cannot enable CUDA at runtime.
For reproducibility, requesting `cuda` on a build or host without CUDA must be a
fatal configuration error; silently falling back to CPU invalidates timing
records.  Every run must print and record the backend actually selected.

The canonical parameter keys are:

```text
compute_backend = cpu   # cpu | cuda | auto
cuda_device = 0
```

`ARCH_ENABLE_CUDA=OFF` creates a CPU-only binary.  `ARCH_ENABLE_CUDA=ON`
creates the prerequisite fat binary, but it does not by itself prove that a
production physics launcher exists for every runtime combination.  At the
current Phase-A/Phase-B milestone, runtime/device probing, all four current
network operators, and an aprox13 BE/Newton/LU correctness baseline are
implemented and tested; the end-to-end CUDA simulation launcher is
intentionally not registered yet.  An explicit
`compute_backend=cuda` request therefore fails loudly instead of silently
executing the CPU driver, while `auto` logs that it selected CPU.

Configure examples:

```bash
cmake -S . -B build-cpu -DARCH_ENABLE_CUDA=OFF
cmake -S . -B build-cuda -DARCH_ENABLE_CUDA=ON \
  -DCMAKE_CUDA_COMPILER=/home/ubuntu/miniconda3/envs/archcuda/bin/nvcc \
  -DARCH_CUDA_ARCHITECTURES=90
cmake --build build-cuda --target ARCH_cuda_aprox13_validation -j 1
```

Use two build profiles:

1. **Validation:** double precision, FMA contraction disabled where necessary,
   assertions and conservation diagnostics enabled.
2. **Performance:** `Release`, `-O3`, normal production FMA settings, diagnostics
   collected without per-cell printing.  The performance build must pass the
   same regression suite, with its own recorded errors.

## Fair CPU/CUDA benchmark matrix

The current JAIST GPU node exposes an NVIDIA H100-20C and 16 virtual Intel Xeon
Gold 6338 CPUs.  Record a fresh hardware snapshot for every benchmark campaign.

| Layer | Workloads | CPU/OpenMP | CUDA | Required output |
| --- | --- | --- | --- | --- |
| Network/ODE microbenchmark | 1K, 16K, 64K, 256K, and 1M independent cells; mild, stiff, and heterogeneous thermodynamic states; all four networks | 1, 2, 4, 8, and 16 threads | kernel-only and end-to-end | cells/s, Newton iterations, accepted/rejected substeps, failures, transfer bytes |
| Hydro-only | `64^2`, `256^2`, `1024^2`, and `2048^2`; burning disabled | same thread sweep | persistent device state | cell-updates/s and time by CFL, reconstruction/flux, update, and boundary kernels |
| Reactive integration | `64 x 16`, `256 x 64`, `1280 x 128`, and `2560 x 256`; identical EOS/network/initial state | same thread sweep | persistent device state | ms/step, simulated-time/wall-time, hydro/burn/NSE fractions, solver failures |
| Physical Cellular run | at least three successively refined meshes that resolve the induction/reaction layer | 16 threads | CUDA | front velocity, induction length, reaction-zone width, cellular scale, conservation, convergence |

For heterogeneous burn batches, report both the original cell order and any
temperature/stiffness-sorted order.  Sorting changes warp divergence and is part
of the algorithm, so its cost must be included in end-to-end timing.

### Timing procedure

- Use identical parameter files, initial-state bytes, EOS table, stopping step
  or physical time, and output policy.
- Set `OMP_PROC_BIND=close` and `OMP_PLACES=cores`; report `OMP_NUM_THREADS`.
- Confirm the GPU is idle.  Record GPU name/UUID, driver, CUDA runtime, clocks or
  power policy when available, CPU model, compiler versions, and affinity.
- Warm the CUDA context and kernels for at least ten representative steps.
  Discard warm-up timing.
- Run at least five independent repetitions and report the median and dispersion
  (MAD or min/max), not only the fastest sample.
- Time CPU regions and end-to-end execution with a monotonic `steady_clock`.
  Time asynchronous CUDA regions with CUDA events and synchronize only at the
  defined measurement boundaries.
- Report separately: parsing/table load, allocation, initial H2D, CFL reduction,
  hydro, burn/ODE/LU, NSE, per-step scalar D2H, scheduled output D2H, final D2H,
  HDF5, and total wall time.
- Report both kernel-only and end-to-end speedup.  A persistent-device solver
  must not be charged a full-state transfer every step unless it actually does
  one.
- Disable plot/checkpoint output and per-step console/file logging in compute
  benchmarks.  Run a separate I/O benchmark with an identical output cadence.
  ARCH currently forces some initial/final output, so a benchmark mode is needed
  before external wall-clock measurements are comparable.

## Numerical acceptance gates

### Microphysics

- Original Fortran versus CPU: RHS, analytic Jacobian including temperature,
  and implicit LHS relative error below `1e-12` for every network.
- CPU versus CUDA validation build: the same three quantities below `1e-12` at
  a matrix of temperatures, densities, compositions, and timesteps.
- NSE: mass and charge residuals below `1e-12`, finite non-negative output, and
  EOS/nuclear-energy closure below `1e-12` on the validated state matrix.

### Integrated CPU versus CUDA

Compare snapshots after 1, 10, and 100 steps and at matched physical times.
For every cell require finite density, momenta, total/internal energy, pressure,
temperature, and species.  Report:

- volume-weighted relative L1, L2, and Linf for every common numeric field;
- maximum absolute error, especially for trace species;
- maximum and RMS error in `sum(X) - 1` and in electron fraction `Ye`;
- global mass, momentum, total-energy, species-mass, and nuclear-energy budgets;
- counts of density/energy floors, caps, rejected burn steps, ODE failures, and
  NSE fallbacks;
- timestep-sequence differences and the first step at which paths diverge.

The comparison tool in `tools/compare_hdf5_fields.py` is report-only unless the
caller supplies tolerances.  Do not assign a universal `1e-12` field tolerance
to evolved shock solutions: an ULP-scale arithmetic difference can move a shock
by a fraction of a cell and dominate pointwise Linf.  The `1e-12` requirement is
appropriate for the isolated microphysics operators.  Full-flow tolerances must
be justified by smooth tests, resolution convergence, and physical diagnostics.

## Comparison with FLASH

Before comparing fields, create a manifest proving that ARCH and FLASH use the
same:

- cgs units, dimensionality, physical bounds, coordinate orientation, and cell
  centers;
- initial thermodynamic state, mass fractions, perturbation/noise formula, phase,
  amplitude, and random seed;
- reflecting/outflow/periodic boundaries and momentum sign conventions;
- isotope set and ordering, nuclear masses/rates, screening and weak-rate options;
- Helmholtz table/constants, Coulomb options, temperature inversion, and
  Helmholtz-consistent initial internal energy;
- NSE thresholds and transition policy, ODE tolerances, energy coupling, source
  splitting, CFL policy, floors/caps, and output physical times.

FLASH block-structured AMR data and ARCH uniform data must be conservatively
restricted/remapped to a common cell-average grid.  Do not use point sampling or
ordinary interpolation across shocks.  Prefer outputs forced to the same physical
time; do not time-interpolate a moving shock field.

At each matched time compare density, all momentum components, total and internal
energy, pressure, temperature, every common mass fraction, `sum(X)`, and `Ye`.
Report unshifted field norms and, separately, shock-aligned norms.  Also compare
front position/velocity, peak pressure and temperature, induction/reaction-zone
width, integrated isotope masses, released nuclear energy, cellular width, and
two-dimensional autocorrelation or spectral scales.  Cross-code agreement is
established by convergence of these quantities with resolution, not by demanding
pointwise `1e-12` agreement from different shock-capturing schemes.

## Required provenance

Every HDF5 output or adjacent JSON manifest used for validation must include:

- repository URL, git commit and dirty status;
- compiler, CMake, CUDA toolkit, driver, build type, architecture, floating-point
  flags, OpenMP/MPI thread/rank counts and affinity;
- selected runtime backend and device UUID;
- complete parameter file and its checksum;
- problem, grid, bounds, boundary conditions, reconstruction, Riemann solver,
  integrator, CFL and source-splitting policy;
- network name/species list, EOS/table path and checksum, NSE and ODE settings;
- initial-state/checkpoint checksum, random seed, start/end time and step count;
- output field schema, units, floor/cap/failure counters, timing-region definitions,
  and checksums of compared files.

Without this provenance, a result may be useful for diagnosis but is not a
reproducible validation or performance result.
