# Verify your ARCH checkout

[中文](README.zh-CN.md) · [Build guide](../docs/guides/Build.md) · [Scientific validation](../validation/README.md)

The checkout includes test code, small independent references, manufactured
problems, validation manifests and network-generation recipes. Build test
executables locally. Generated packages, external libraries, large simulation
outputs and profiler data are not bundled as prebuilt dependencies. Run
`git lfs pull` before tests or cases that use the tracked EOS table.

ARCH tests use repository-owned analytical/independent references and ordinary
build dependencies. Configuring tests, running them and completing release
acceptance require no FLASH or other simulation program. Cross-code comparisons
are optional, separate manual work for maintainers.

All commands below run from the repository root in Linux or WSL2. Tool tests,
numerical regressions and application smoke have different purposes;
[Validation](../validation/README.md) records scientific comparisons and budgets.

During development, build the affected existing targets and run their ordinary
numerical and interface checks first. When a check fails, identify the input,
calculation or wiring error before adding the smallest reproducing witness;
do not launch a larger simulation matrix for the same unresolved failure.
After ordinary checks pass, extend the relevant existing owner with missing
edge coverage: near vacuum, axis-adjacent cells, strong rotation, nonfinite
inputs, recovery after rejection, and AMR restart. Retain the analytical-error,
conservation and convergence criteria.

Completed evidence may be reused for the same source, input and executable
identities. Rerun affected checks after mathematical or call-path changes;
run the complete inventory once at source freeze, before CUDA, whole-run timing
and long-run acceptance. Keep independent references separate from production
calculations. Prefer assertions in existing targets over another compiled copy
of the production algorithm or another run of the same simulation group.

For conservative-state acceptance changes, the focused Host targets are
`arch_conservative_acceptance`, `arch_rkl_repair_weights` and
`arch_shared_stage_scheduler`. They check bounded composition corrections and
receipts, propagation through the existing RKL recurrence, and rejection before
state publication. `arch_cuda_conservative_acceptance` runs the same acceptance
leaf on an actual device. These contracts do not replace complete AMR evolution
and restart; [RT replay inputs](../validation/amr/inputs/rt/README.md) provide the
four reported cases and their current evidence owner.

`arch_ppm_limiter` checks the common PPM face projection and curvature-supported
profile blend against analytic constant, affine and quadratic cell averages and
roundoff-perturbation witnesses. `arch_cuda_ppm_limiter` exercises the same
production function on a real GPU, with bitwise Host/device comparisons and the
same perturbation budget. These leaves complement the existing smooth-wave,
Sod and Sedov scientific references; they are not full-evolution acceptance.

`arch_flux_limiter` checks conservative face limiting at zero and trace
composition, including strict fluid bounds, conservation, scaling and physical
reflection. `arch_cuda_flux_limiter` compares the same production leaf on Host
and device. Both use the existing composition roundoff band; neither introduces
a physical species floor or replaces stage acceptance and correction receipts.

Compiling the full test suite costs more time and host RAM than building only
the application. `BUILD_TESTING=ON` registers the applicable tests; the default
`all` build compiles their extra executables. CUDA tests add substantial NVCC
compilation when `ARCH_ENABLE_CUDA=ON`. This is primarily host-memory pressure,
not GPU-memory use; GPU memory matters when the device tests actually run.
Larger generated networks and additional GPU target architectures add more
compilation work.

Both Release presets keep `BUILD_TESTING=OFF` for a first application build.
New users can start with `cpu-release` and the tool checks below, then enable
tests or choose `cuda-release` as needed. With testing enabled,
`cmake --build <build-dir> --target ARCH` still builds the application and its
dependencies, not standalone tests. Changing a `.par` file to run on CPU does
not remove the compile cost of a CUDA-enabled build.

## GitHub continuous integration

[ARCH CI](../.github/workflows/ci.yml) runs the shared-authority audit, the full
Python tooling suite and a CPU Release build with all configured Host tests,
including KLU, on GitHub-hosted Linux machines. The jobs reuse the commands and
test owners described here; no separate CI mathematical implementation exists.
The CPU job fetches the required Helmholtz LFS table and rejects incomplete or
skipped CTest reports. The tooling job also rejects skipped controls.
Runner contracts labeled `tooling` execute in the Tooling job; the CPU job uses
the same label exclusion for inventory and execution. An unfiltered local
CTest run below includes those contracts.

Studio and Host run their full Node suite once, followed by lint and the typed
production build. The `CI required` result combines all three jobs. It does not represent CUDA
execution or a rerun of the full scientific Validation campaign. Diagnostic
artifacts are kept for 14 days, separately from reviewed Validation records.
See the [workflow guide](../.github/workflows/README.md) for manual runs,
resource limits, security settings and branch-rule setup.

## Check tools without a GPU

Use Python 3.10 or newer and Git/CMake from the build setup. Install NumPy and
h5py for the microphysics/checkpoint/Plotfile protocol checks. The suite uses controlled
fixtures and does not need pynucastro, SciPy or a CUDA device.

```bash
python3 tools/audit_architecture.py .
python3 -m unittest discover -s tests/tooling -p 'test_*.py'
```

Full resource-guard coverage needs Linux `/proc`, child-process ownership support
and Python's `os.pidfd_open`. An unsupported Python build or kernel may skip those
controls. Read the summary: a skip does not verify that feature.

The architecture audit prunes configured CMake build trees and auxiliary Git
worktrees before scanning. It still reads new/untracked source and user modules
under source directories; it does not rely on a clean Git index or ignore a
broad `build*` prefix. Running from a normal development checkout therefore does
not require exporting a temporary source snapshot.

## Build and run CPU regressions

Prepare the dependencies in the [build guide](../docs/guides/Build.md). Default
KLU support includes CPU sparse solving; configuration may fetch HighFive and
SuiteSparse if they are not installed.

```bash
cmake -S . -B build-test-cpu -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DARCH_ENABLE_CUDA=OFF \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-test-cpu/bin"
cmake --build build-test-cpu --parallel 1
ctest --test-dir build-test-cpu -N
ctest --test-dir build-test-cpu --parallel 1 --output-on-failure
```

CTest does not compile missing executables. The default build above includes
configured tests. For a quick checkpoint-only check, build
`arch_checkpoint_compatibility` and select `-R '^checkpoint_compatibility$'`.
It checks complete ARCH state restoration and rejected input through the same
reader used by both backends. Current-format round trips, explicit old-control
identity rejection and corrupt-payload rejection are tested together; rejected
reads preserve the live AMR state, and validation failures before a write
preserve the previous checkpoint file.

`arch_cuda_single_level_validation` is also available in CPU-only builds. The
historical target name is retained for runner compatibility, but the utility
uses ordinary C++ and the Host HDF5 reader and does not link the CUDA backend.
Its comparison rules and `checkpoint_temporal_comparison` test are unchanged.

For a scoped Driver CUDA check, `tools/check_ci_results.py --profile driver-cuda`
requires scheduler, gravity preparation, checkpoint and CUDA AMR/batch/reduction
coverage anchors. Supply inventory and JUnit files from the same CTest selection;
every selected test must pass without skips. This does not certify the full CUDA
inventory, application-level numerics, restart or sanitizer checks. Hosted CPU CI
continues to check its complete numerical/API inventory, with runner contracts
covered by the separate Tooling job.
Build this target to compare CPU artifacts without a CUDA build.

## Add CUDA and its sparse provider

Prepare the NVIDIA driver/toolkit and cuDSS using the
[CUDA build guide](../docs/guides/Build.md#cudss-and-sparse-burning).
Set `CUDSS_ROOT` for a custom installation; a system-wide install is not required.
Keep KLU if this build should also test CPU sparse burning.

```bash
cmake -S . -B build-test-cuda -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DARCH_ENABLE_CUDA=ON -DARCH_ENABLE_CUDSS=ON \
  -DCMAKE_CUDA_ARCHITECTURES=native \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-test-cuda/bin"
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-test-cuda --parallel 1
ctest --test-dir build-test-cuda -N
ctest --test-dir build-test-cuda --parallel 1 --output-on-failure
```

`native` targets the visible GPU; use an explicit architecture selection when
building for another device. Build parallelism is configurable and does not
change numerical methods. Begin with serial device tests and inspect their
memory requirements before increasing concurrency.

The same resource guard can observe logical output size and actual free space
on the Host storage mount. Create an output parent before launching a simulation
that creates its own run directory below it:

```bash
mkdir -p validation-local
/usr/bin/python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard \
  --output-root validation-local --max-output-mib 512 \
  --host-storage-root /mnt/e --min-host-free-mib 8192 \
  --next-write-reserve-mib 64 -- ./build-test-cpu/bin/ARCH Sod input.par
```

The mount and capacities are local examples; set `out_dir` in `input.par` below
the observed output directory. The guard requires Python with Linux pidfd
support. It refuses or stops a run when output plus the next-write reservation
exceeds the budget, or Host free space falls below the retained margin plus that
reservation; normal completion also checks the final sample. WSL filesystem
free space and the free space of its Host disk are recorded separately. The
observation is sampled, and the caller sets the reservation from expected output.
After a batch passes, save processed metrics and input/binary identities before
removing raw output. Retain failed and unresolved evidence in a bounded local
directory.

Check CMake's provider messages and the CTest inventory. cuDSS-specific and
generated-network tests appear when their providers/packages are enabled.
A build without those entries does not test those capabilities. For the full
representative profile, generate audit31 and the weak network from the
[network setup and recipes](../validation/network/README.md#reproduce-the-records),
then reconfigure with those package IDs selected. pynucastro is needed for
generation, not for running ARCH. Large audit150/audit200 workloads are separate
scaling exercises, not required for getting started.

## Check the application and restart

Use ARCH and its comparator from the same build:

```bash
python3 tools/smoke_cuda_amr_runtime.py \
  --arch build-test-cuda/bin/ARCH \
  --checkpoint-validator build-test-cuda/arch_cuda_single_level_validation
```

The [smoke guide](smoke/README.md) describes CPU/CUDA dynamic AMR, curved diffusion
and short checkpoint continuation. For field-by-field recovery and continued
evolution, use the [restart checks](../validation/restart/README.md#reproduce-the-dynamic-amr-checks).
They cover all four backend directions, native composition, ENUC, controller
state and output phase. Runners retain logs and require an absent or empty output
directory; they do not silently overwrite another run.

Shared application runners read HDF5 through the C++ comparator. Independent
scientific references may additionally need NumPy, SciPy, mpmath or h5py;
each [Validation module](../validation/README.md) lists its requirements.
Compute Sanitizer is needed only for instrumented checks.

## Find or extend a test

- [host/](host/README.md): CPU contracts, AMR/geometry, checkpoint, EOS and burn.
- [cuda/](cuda/README.md): device execution, parity, memory and transaction lifecycles.
- [tooling/](tooling/README.md): audits, build metadata, runners, provenance and guards.
- [math/](math/README.md): numerical witnesses shared by host and device.
- [fixtures/](fixtures/README.md): independent data and controlled systems.
- [smoke/](smoke/README.md): short complete-application checks.

[CMakeLists.txt](../CMakeLists.txt) selects the test groups; target declarations
and conditions live in [HostTests.cmake](../cmake/tests/HostTests.cmake) and
[CudaTests.cmake](../cmake/tests/CudaTests.cmake).
Keep one copy of shared test data and place production algorithms
in `src/`. Derive reference results independently of the routines being tested.


### Self-gravity checks

The CPU anchors are `composite_poisson_contract`, `composite_poisson_analytic`,
`self_gravity_lifecycle` and `self_gravity_physics`. The last test runs the real
executable and requires numpy/h5py in CMake's selected `Python3_EXECUTABLE`
environment (CI installs both). Configure that interpreter with the matching
packages rather than skipping the test. Device, coupling and performance evidence
is in [gravity validation](../validation/gravity/README.md).

User-boundary registration, EOS states, coordinate normals, stage times and
conservative surface fluxes extend the existing `boundary_plan` test. The
existing `compute_backend` entry checks parallel Host surface evaluation,
corner/snapshot parity and rejection before scatter. Mixed
potential conditions, Gauss compatibility and cache invalidation extend the
Poisson/gravity tests above. `self_gravity_physics` also runs short three-geometry
boundary, AMR and restart checks through the same test entry point. Full device
comparisons and timing remain manual gravity-validation campaigns; raw HDF5
stays local.
