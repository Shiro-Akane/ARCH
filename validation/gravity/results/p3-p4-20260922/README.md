# P3/P4 composite CPU self-gravity acceptance

Date: 2026-09-22 (JST). Branch: `physics/selfgravity`.
Parent: `3c73cca2c5a4ab81d30ca2cc309db4d25a7a6feb` (pushed P2).
The candidate source is the commit containing this record; executable identities
are recorded separately because CPU Debug and CUDA Release are different builds.

The [implementation contract](../../../../docs/development/P3P4CompositeGravity.zh-CN.md)
defines the algorithms, acceptance budgets, ownership and supported combinations.
This record qualifies the basic periodic Cartesian CPU route, including composite
AMR, stage coupling and restart. It does not qualify isolated boundaries, GPU
self-gravity, or long-duration three-dimensional astrophysical applications.

## Build and environment

- CPU: GCC 12, C++20, Debug, OpenMP, KLU enabled; CUDA disabled. The existing
  `build` directory produces `bin/ARCH`. No fast math or floating-point contraction.
- Independent 3D operator/resource witness: GCC 12, `-O2 -fno-fast-math
  -ffp-contract=off`; `/tmp/arch-composite-poisson`.
- CUDA compatibility: GCC 12, CUDA 12.3, Release, architecture 86, KLU/cuDSS
  enabled; `build/selfgravity-p1-gpu/bin/ARCH` remains separate from the CPU binary.
  Device: RTX 3060 Ti, 8192 MiB, driver 610.47. CUDA self remains unsupported.
- Host: 16 logical CPUs, approximately 7.7 GiB RAM and 2 GiB swap. CUDA build
  uses six jobs and the existing three-job heavy-compilation pool, guarded at
  256 MiB available memory / 1536 MiB additional swap.
- Python: `/usr/bin/python3` 3.12 with numpy/h5py from `/tmp/arch-p15-python`.
  Reproduction elsewhere should use that interpreter's installed packages,
  not these machine-specific dependency paths.

## CPU acceptance

| Check | Result / evidence |
|---|---|
| Complete current CTest inventory | **61/61 passed, no skips**; [log](cpu-ctest.log), [JUnit](cpu-ctest.xml), [inventory](cpu-inventory.json); CI inventory gate passed |
| Final focused regression | **5/5 passed**; [log](final-focused.log) |
| Native vector field mapping | Actual 2D/3D layouts, nonzero force on every active axis; [log](native-vector-components.log) |
| Composite manufactured solutions | **18 runs**: 1D/2D/3D, uniform/refined, 16/32/64 root cells per axis; [log](composite-3d.log) |
| Composite failure and invariant contracts | Constant nullspace, conservative subface flux, hierarchy volumes, invalid topology, nonfinite/incompatible RHS, nonconvergence, nested levels and tiny/large amplitudes; [log](composite-contract.log) |
| Physical application | **38 acceptance records**; [summary](physical/summary.json), [log](physical.log), per-case inputs and logs in `physical/` |
| Address/undefined-behavior/leak sanitizers | Lifecycle, native ownership and near-vacuum source witness; [log](sanitizers.log) |
| CI tooling / architecture tooling | **10/10** and **104/104**; [CI log](ci-tooling.log), [architecture log](architecture-tooling.log) |

The last native vector-component test was added after the complete CTest run;
its rebuilt lifecycle executable passed separately and in the final five-test focused
rerun. The final production sources were already present for the full suite,
focused suite and 38 physical records.

An initial CTest invocation used a cached Python 3.14 interpreter with Python 3.12
h5py packages. Only the new Jeans Python test failed to import h5py; this is retained
in [the environment-failure log](cpu-python-environment-failure.log) and its JUnit file.
CMake was then configured with `/usr/bin/python3`; the entire 61-test inventory was
rerun successfully. No numerical budget or pass criterion was changed to resolve it.

## Numerical results

- Finest mixed 3D manufactured mesh: 491,520 cells, 17 outer iterations; final
  potential / face-force / interface-force orders **2.0002 / 2.0357 / 2.0207**.
  All applicable orders meet the frozen 1.8 target. The complete O2 operator group
  took 12.12 s wall time with 721,952 KiB peak RSS; see [resource log](composite-resources.txt).
- Manufactured mixed 3D normalized net self-force: `2.61e-4`, `6.80e-5`, `1.71e-5`
  across the three resolutions. The conservative flux operator is not claimed
  symmetric; net self-force is checked separately and decreases under refinement.
- Jeans wave at 32/64/128 root cells: density perturbation relative RMS errors
  **0.6569% / 0.1596% / 0.04652%**. Standard 64-cell runs satisfy the 2% budget.
- Temporal orders against an independent finer RK3 trajectory:
  Euler **1.023/1.012**, RK2 **2.004/2.002**, RK3 **3.004/3.002**.
  Halving the reference step changes it by less than 1% of the smallest measured
  temporal error. This check uses a fixed PCM spatial operator.
- Standard 64-cell total gas-plus-gravity energy errors, normalized to initial
  gravitational potential energy: Euler **0.4875%**, RK2 **0.00000120%**,
  RK3 **0.000642%**, all below 1%. The coarse 32-cell Euler time-order probe at
  CFL 0.8 has **10.3%** energy error: it is a time-order probe, not an energy-qualified
  standard run. The dataset retains its error; no universal conservation claim is made.
- Dynamic 1D AMR at 64/128 root cells: refine **6/23**, coarsen **4/20**, and no-change
  operations observed. Mass drift `2.80e-14/5.65e-14`, potential-energy-normalized
  energy error `8.55e-5/4.31e-5`, maximum sampled net self-force
  `8.96e-8/6.79e-9`. Smooth accepted states require zero repairs.
- Actual 2D/3D multiblock uniform and mixed-level Driver runs complete two RK3 steps.
  A separate product-density lifecycle test exercises all acceleration components.
  Long-time dynamic evolution qualification in this record is **1D**.
- Dynamic AMR continuous/restart endpoints match **14 checkpoint datasets bitwise**.
  Changes to G, relative/absolute tolerance or maximum iterations reject restart.
  Checkpoint v6 stores gravity provenance, while potential and acceleration are
  rebuilt from density; v5 compatibility is intentionally not retained.

## Reproduction

From the repository root, with an existing testing-enabled CPU build and Python
numpy/h5py available:

```bash
cmake -S . -B build -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build --target ARCH arch_composite_poisson arch_self_gravity -j 4
ctest --test-dir build --output-on-failure -j 4 --output-junit cpu-ctest.xml
build/arch_composite_poisson 3
build/arch_composite_poisson contract
build/arch_self_gravity
python3 validation/gravity/run_self_gravity.py \
  --arch bin/ARCH --output /tmp/arch-gravity-physical-new
```

Use a new physical output directory. The CPU executable SHA-256 in
`physical/summary.json` identifies the actual tested application. HDF5 outputs
remain local/ignored; committed configurations and scripts reproduce them.
The separate optimized resource witness is built from the same operator test
and `CartesianPoisson.cpp`, `CompositePoisson.cpp`, `HostMultigrid.cpp`, and
`HostCompositeMG.cpp`, with `-std=c++20 -Isrc` and the O2 flags above.

The sanitizer executable uses the lifecycle test, AMR adapter and the same math
sources plus `SelfGravity.cpp`, compiled with `-O1 -g -fsanitize=address,undefined
-fno-omit-frame-pointer -fno-fast-math -ffp-contract=off`. Run with
`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1` outside ptrace-restricted
sandboxing. The later vector-component-only test addition was checked normally;
it is not included in the saved sanitizer claim.

## CUDA compatibility

The Release CUDA application and native checkpoint validation utility built
successfully: **110/110 actions**, approximately 1900 s, peak build-process RSS
4,939,900 KiB, minimum available host memory 1,000,496 KiB. Additional swap was
219,436 KiB and the memory guard did not stop the build; see [build log](cuda-build.log).
Existing upstream/template warnings are retained in that log.

All **six compatibility records passed**; see [summary](cuda-compatibility/summary.json)
and [runner log](cuda-compatibility.log). External gravity reaches the analytic
`t=0.1` solution with maximum relative field error `2.3627e-16` on both builds.
The CUDA run records 1120 device kernels; CPU checkpoint -> CUDA continuation
records 1056 device kernels and the same analytic accuracy. Explicit CUDA self
fails before checkpoint output; auto self resolves to CPU and publishes nonzero
potential. Binary identities are included in the summary and [manifest](manifest.json).
These are compatibility runs, not a self-gravity GPU speed comparison.

Reproduce the scoped checker with:

```bash
python3 validation/gravity/check_cuda_compatibility.py \
  --cpu-arch bin/ARCH --cuda-arch build/selfgravity-p1-gpu/bin/ARCH \
  --output /tmp/arch-gravity-cuda-compat-new
```

Successful compilation or existing CUDA hydro execution does not establish GPU
self-gravity correctness or acceleration. P5/P6 remain separate work.
