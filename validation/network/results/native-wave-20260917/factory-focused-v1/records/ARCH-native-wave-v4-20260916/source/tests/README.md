# Verify your ARCH checkout

[中文](README.zh-CN.md) · [Build guide](../docs/guides/Build.md) · [Scientific validation](../validation/README.md)

The checkout includes test code, small independent references, manufactured
problems, validation manifests and network-generation recipes. Build test
executables locally. Generated packages, external libraries, large simulation
outputs and profiler data are not bundled as prebuilt dependencies. Run
`git lfs pull` before tests or cases that use the tracked EOS table.

All commands below run from the repository root in Linux or WSL2. Tool tests,
numerical regressions and application smoke have different purposes;
[Validation](../validation/README.md) records scientific comparisons and budgets.

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

The `CI required` result combines those two jobs. It does not represent CUDA
execution or a rerun of the full scientific Validation campaign. Diagnostic
artifacts are kept for 14 days, separately from reviewed Validation records.
See the [workflow guide](../.github/workflows/README.md) for manual runs,
resource limits, security settings and branch-rule setup.

## Check tools without a GPU

Use Python 3.10 or newer and Git/CMake from the build setup. These checks use
the standard library and controlled fixtures; they do not need pynucastro,
NumPy, SciPy or a CUDA device.

```bash
python3 tools/audit_architecture.py .
python3 -m unittest discover -s tests/tooling -p 'test_*.py'
```

Full resource-guard coverage needs Linux `/proc`, child-process ownership support
and Python's `os.pidfd_open`. An unsupported Python build or kernel may skip those
controls. Read the summary: a skip does not verify that feature.

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
reader used by both backends.

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
