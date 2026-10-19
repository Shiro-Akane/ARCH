# Test ARCH after checkout

[中文](README.zh-CN.md) · [Build](../docs/guides/Build.md) · [Scientific validation](../validation/README.md)

Engineering tests check mathematical leaves, interfaces, lifecycle and failure handling. Validation adds independent references and actual application endpoints. Tests do not require FLASH or another simulation program. Reuse accepted evidence only within its source/input/binary scope; rerun the owners affected by a change.

## Choose the smallest meaningful check

| Change | Entry | What it establishes |
| --- | --- | --- |
| Validation/build/report tooling or docs | `python3 -B tests/tooling`; architecture/documentation audits below | Complete runner/identity/resource and documentation contracts, not physical accuracy |
| CPU mathematics, configuration, API, AMR or IO | Existing [Host](host/README.md) / API target and CTest | Its independent mathematical or interface assertions |
| CUDA indexing, launches, allocation or residency | Existing [Device](cuda/README.md) target and applicable full-program short check | Actual device execution for that scope; Host results do not substitute |
| Studio/Host | [Studio checks](../studio/tests/README.md) | Typed responses, request association, owned lifecycle and rendering/build contracts |
| Scientific endpoint, convergence or coupled evolution | Owning [Validation](../validation/README.md) manifest/reference | Original error, conservation, topology and fresh-continuation requirements |

Extend an existing assertion owner before creating another executable or probe. Keep independent references independent of production mathematics. Failure diagnosis uses a small reproducer; a larger matrix cannot explain an unresolved small failure. Long-run, sanitizer and fair CPU/GPU cost campaigns stay outside every-PR testing.

## Tooling and current documentation

Use Python 3.10+ with NumPy/h5py, Git and CMake. Linux resource tests need `/proc`, pidfd and child-process support. This workstation uses the existing `work` environment for scientific Python; the resource suite uses the system Python that provides pidfd. A missing facility or skipped control is not complete verification.

```bash
python3 tools/audit_architecture.py .
python3 tools/check_documentation.py .
python3 -B tests/tooling
```

The complete Tooling entry rejects an empty, failed or skipped suite. It includes the sparse trajectory transcript controls; dependency-heavy independent oracle tests remain documented in their Validation owner. Current Markdown destinations/headings are checked without rewriting dated/frozen evidence.

## Compile and run CPU regressions

First follow [Build](../docs/guides/Build.md), including [offline table identity](../docs/guides/Build.md#offline-tables-and-lfs-bandwidth). Default presets build the application with testing disabled; building `--target ARCH` does not build standalone tests. CTest never compiles missing programs.

```bash
cmake -S . -B build-test-cpu -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DARCH_ENABLE_CUDA=OFF \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-test-cpu/bin"
cmake --build build-test-cpu --parallel 2
ctest --test-dir build-test-cpu --show-only=json-v1
ctest --test-dir build-test-cpu --parallel 1 --no-tests=error --output-on-failure
```

Use the existing memory guard for a heavy build and choose concurrency by measured process-tree RAM. Shared dispatch/LTO work can consume several GiB even with one outer job. For a focused change, build its target and select its exact CTest with `-R '^name$'` before broadening.

The unfiltered command includes the single `tooling_contracts` entry. If Tooling has already run, use `-LE '^tooling$'` in both discovery and execution to avoid running it twice. Other configured tests remain selected; do not use a name filter as a claim of full CPU coverage.

The Host-only `arch_cuda_single_level_validation` comparator is available without CUDA. Its historical name does not imply a device dependency. `checkpoint_compatibility` and `checkpoint_temporal_comparison` check their original restoration/rejection and comparison contracts.

## CUDA and optional providers

Use a separate build tree and the [CUDA build instructions](../docs/guides/Build.md). Enable cuDSS only when its actual library is available; KLU remains the CPU sparse provider. `native` targets the visible GPU, not every supported platform.

```bash
cmake -S . -B build-test-cuda -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DARCH_ENABLE_CUDA=ON -DARCH_ENABLE_CUDSS=ON \
  -DCMAKE_CUDA_ARCHITECTURES=native \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-test-cuda/bin"
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-test-cuda --parallel 2
ctest --test-dir build-test-cuda --parallel 1 --no-tests=error --output-on-failure
```

Retain CUDA `RUN_SERIAL`, provider conditions, device-unavailable codes and compile-pool limits. A skipped device test is not accepted GPU qualification. Generated-package tests appear only for packages actually discovered/configured; an empty package selection means all discoverable packages, not disabled generation. Setup and independent weak/NSE references belong to [network validation](../validation/network/README.md).

## Full-program checks and cost

Use an ARCH executable and comparator from the same build. [Smoke](smoke/README.md), [restart](../validation/restart/README.md) and [gravity](../validation/gravity/README.md) own bounded application, fresh-process and optional-field checks. Their manifests define the real endpoint, input, fields and budgets. A step diagnostic cannot replace a required physical-time comparison.

Freeze source/configuration before measuring; record build/run wall time, compiler/profile, threads, RAM/swap and exit reason. Reuse matching accepted science rather than rerunning it after pure comments. Abnormally slow runs may stop under the existing guard with reached step/time and retained evidence; truncation is never PASS. Before HDF batches check actual host storage, not WSL's virtual free space; this workstation warns near 1 GiB remaining.

## GitHub continuous integration

[CI](../.github/workflows/README.md) runs Tooling once, applicable complete CPU/KLU and Studio lanes, and one required aggregate. Recognized documentation changes can omit expensive native/Node work with explicit `NOT_APPLICABLE`. Unknown scope selects full execution. Inventory/JUnit and TAP checks reject empty, missing, failed or skipped controls. Local success is not hosted execution or scientific release qualification.

## Find and extend tests

[Host](host/README.md), [Device](cuda/README.md), [Tooling](tooling/README.md), [math](math/README.md), [fixtures](fixtures/README.md) and [smoke](smoke/README.md) describe the existing owners. Registration is in [HostTests.cmake](../cmake/tests/HostTests.cmake) and [CudaTests.cmake](../cmake/tests/CudaTests.cmake). Keep the accepted RKL core/Tabular split and grouped burn routes; retain every independent subcase and original threshold when sharing fixtures or registration.
