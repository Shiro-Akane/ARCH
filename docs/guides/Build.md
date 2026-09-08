# Building ARCH

[中文](Build.zh-CN.md) · [Quick start](../../README.md#build)

Use the README's CPU or CUDA commands for a first build. This guide explains
the choices behind them and how to adjust compilation for your machine. Run
the shell commands from the repository root in a Linux or WSL2 terminal.

## Configure first, then compile

`cmake --preset cpu-release` **configures** a build: it locates your compilers and dependencies, saves your options to `build-cpu/CMakeCache.txt`, and generates the Ninja build instructions. This step does not compile the ARCH executable yet. The preset supplies the usual options; `cmake -S . -B build-cpu -G Ninja ...` lets you provide them individually.

`cmake --build build-cpu --target ARCH --parallel 1` **compiles and links** the application and its dependencies. CMake will automatically invoke Ninja for you, meaning there is no separate `make` step. After you edit source code, simply repeat this build command; Ninja will smartly recompile only the affected objects and links. If you need to change a build option, repeat the configuration step first. Always use a fresh build directory when switching to a different compiler or generator.

The presets in [CMakePresets.json](../../CMakePresets.json) keep output separate:

| Configure command | Build directory | Executable |
| --- | --- | --- |
| `cmake --preset cpu-release` | `build-cpu` | `build-cpu/bin/ARCH` |
| `cmake --preset cuda-release` | `build-cuda` | `build-cuda/bin/ARCH` |
| `cmake --preset cuda-debug` | `build-cuda-debug` | `build-cuda-debug/bin/ARCH` |

All three use Ninja and OpenMP. The Release presets set `BUILD_TESTING=OFF`;
`cuda-debug` enables tests for development. Both CUDA
presets target the visible GPU with `CMAKE_CUDA_ARCHITECTURES=native` and set
`ARCH_CUDA_HEAVY_COMPILE_JOBS=1`. They configure builds only: use the explicit
build and memory-guard commands below to compile. Run `cmake --list-presets`
to see the available choices.

Start with `cpu-release` for a first simulation, then use `cuda-release` when
you need GPU execution. The CUDA presets enable `ARCH_ENABLE_CUDA=ON`. This
significantly increases compile time compared with the CPU-only build: NVCC
must instantiate and compile device routes and complete their linking in
addition to the host application. The extra compilation uses host CPU and
RAM; having more free GPU memory does not remove that host-memory pressure.
Larger generated networks and more GPU target architectures further increase
the work. Actual duration depends on the selected targets, compiler cache,
parallelism and machine, rather than a fixed CUDA build time.

`compute_backend` in a `.par` file is a runtime choice. Selecting `cpu` there
does not make an `ARCH_ENABLE_CUDA=ON` build CPU-only or skip its CUDA compilation.
Use the CPU preset when you want a build without that work.

Output separation comes from `ARCH_RUNTIME_OUTPUT_DIRECTORY`, set to each
build directory's absolute `bin/` path. Without that option, the project writes
executables to `bin/` in the source tree. Append `-DNAME=value` to a configuration
command to override a preset option, such as a compiler or CUDA architecture.

## Build output

Configuration normally shows ARCH's selected backend, optimization settings
and dependency results. When KLU is built from SuiteSparse sources, repeated
per-library status reports are hidden by default. Notices, warnings and errors
remain visible, and the log level is restored before ARCH reports the KLU
result. Installed-library discovery is unchanged.

Debug controls how the program is compiled, not how much configuration text is
printed. To inspect the complete dependency configuration, append
`--log-level=VERBOSE` to the configuration command. An explicit
`CMAKE_MESSAGE_LOG_LEVEL` is also respected. `ARCH_VERBOSE_BUILD=ON` restores
the default dependency reports and enables full commands for Makefile builds.
With any generator, use `cmake --build <build-directory> --verbose` when you
need the actual compiler and linker commands. These output choices do not
change optimization, floating-point settings, linked libraries or parallelism.

## Tools and dependencies

Install Ninja for the commands in this guide. CPU builds need a C++20 compiler,
CMake **3.22 or newer**, HDF5 development
libraries with the C++ and HL components, and OpenMP unless
`ARCH_ENABLE_OPENMP=OFF`. A CPU-only build does not need a CUDA toolkit.

CUDA builds additionally need CMake **3.25.2 or newer**, NVIDIA's CUDA toolkit
with **NVCC 12.0 or newer**, a host compiler supported by that toolkit, and a
compatible NVIDIA driver. The reference build used CMake 3.28, CUDA 12.3 and
GCC 12. CMake's CUDA C++20 support is described in its
[3.25.2 release notes](https://cmake.org/cmake/help/latest/release/3.25.html).

For WSL2, the NVIDIA driver belongs on Windows; install the Linux CUDA Toolkit
inside WSL without a Linux display driver. See
[NVIDIA's WSL installation guide](https://docs.nvidia.com/cuda/wsl-user-guide/index.html).

Git and network access are needed when configuration first fetches HighFive
2.9.0. KLU is enabled by default: CMake uses an installed KLU package or fetches
SuiteSparse 7.13.0. If dependencies are already prepared locally, CMake's
FetchContent source overrides can point to them; the OpenMP, HDF5, HighFive and
KLU declarations are in [cmake/Dependencies.cmake](../../cmake/Dependencies.cmake).
CUDA-specific provider discovery, including cuDSS, remains in
[cmake/CudaBackend.cmake](../../cmake/CudaBackend.cmake), using
[FindCuDSS.cmake](../../cmake/FindCuDSS.cmake). Git LFS supplies the EOS assets
tracked through LFS; run `git lfs pull` before using those tables.

Python **3.10 or newer** is needed for the build guard and test tooling, not for
running the ARCH executable. Generated reaction networks have a separate Python
setup described in the [network guide](../../validation/network/README.md).

## cuDSS and sparse burning

`ARCH_ENABLE_CUDSS=ON` enables optional discovery in a CUDA build. The adapter
uses the **cuDSS 0.8 API** and checks that the loaded library matches the header
major/minor version. Finding no library leaves the CUDA sparse provider out of
the build; requests that need it then report an unsupported configuration.
Other CUDA features and compact DenseLU burning do not require cuDSS.

No root installation is required. CMake searches its usual locations and the
`CUDSS_ROOT`/`CUDSS_DIR` environment hints. If you installed cuDSS under a
user-local prefix containing `include/cudss.h` and `lib/` or `lib64/`, set that
prefix explicitly. For example, **after installing it at this location**:

```bash
cmake -S . -B build-cuda -DCUDSS_ROOT="$HOME/.local/cudss"
```

Omit this option when discovery already finds the library. Configuration prints
`[DEP] cuDSS found:` with the selected library, or states that the sparse provider
is unavailable. `ARCH_ENABLE_CUDSS=OFF` deliberately disables discovery. Keep
the default `ARCH_ENABLE_KLU=ON` if the same executable should also support
sparse burning on CPU: KLU is CPU-only, and cuDSS is CUDA-only. The selected
network/EOS route must also be compiled; library discovery alone does not add
custom networks.

## Parallel compilation and memory

We recommend starting with the quick-start settings: Ninja, `ARCH_CUDA_HEAVY_COMPILE_JOBS=1`, and `--parallel 1`. The first option caps the pool of heavy CUDA/dispatch compilation jobs, while the second strictly caps the total number of concurrent build jobs. Ninja enforces both limits simultaneously. Keep in mind that other CMake generators do not respect this project-specific job pool.

Use the memory guard around a CUDA build:

```bash
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-cuda --target ARCH --parallel 1
```

The guard stops its build if available Linux RAM falls below 1.5 GiB, observed
swap grows by more than 256 MiB from its starting value, or sustained memory/I/O
stalls cross its pressure limits. Existing swap is not required to be zero.
The default pressure limits are 20% memory full-stall or 50% I/O full-stall for
10 consecutive seconds. These are Linux PSI measurements, not Windows disk
utilization percentages. Sampling is a safety aid, not a reservation of RAM.

The guard needs readable `/proc` data, Linux memory and I/O PSI `full` counters,
and working Python pidfd and Linux child-subreaper support. It checks these
before launching work. If a WSL/kernel environment lacks them, update that
environment before using this guarded workflow. `Ctrl+C` or `SIGTERM` stops the
owned build and compiler descendants. Add `--log build-cuda/build-memory.log`
before `--` to save output and measurements; the log path must be new.

The measured mid-range reference is WSL2, an i7-10700-class CPU, **16 GB system
RAM** and an RTX 3060 Ti-class **8 GB GPU**. It completed the core build with
**two heavy jobs and four total jobs**. To try that setting after the initial
configuration:

```bash
cmake -S . -B build-cuda -DARCH_CUDA_HEAVY_COMPILE_JOBS=2
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-cuda --target ARCH --parallel 4
```

This is a measured comparison point, not a minimum requirement or a universal
optimum. The [core-build record](../../validation/backend/results/cold-core-first-law-20260907/release-909/README.md)
specifies its networks and workload. Check WSL's own allocation with `free -h`:
installed Windows RAM is not necessarily all assigned to Linux. Leave memory
for Windows and other applications. Larger-memory systems can raise the job
limits and compare the guard's measurements. Compilation limits do not change
the simulation's numerical parameters or reserve runtime GPU memory.

## Optimization, compilers and target GPUs

Release builds retain full CPU optimization and enable LTO/IPO when supported. Ensure you select matching GCC major versions for both `CMAKE_C_COMPILER` and `CMAKE_CXX_COMPILER`, and point `CMAKE_CUDA_HOST_COMPILER` to that same C++ compiler. You should set these during the very first configuration of a fresh build tree; any locally compiled dependencies must also be built with compatible LTO toolchains. Note that you don't need to disable LTO just to change build parallelism. Finally, to strictly preserve numerical behavior, our shared floating-point contract deliberately disables fast-math and contraction across all supported toolchains.

CMake automatically uses `ccache` when found, including for CUDA compilation.
It can shorten repeated builds; the recorded cold-build measurements disabled
the compiler cache. Changes to widely included headers can still rebuild many
translation units.

`CMAKE_CUDA_ARCHITECTURES=native` targets the GPU visible during configuration.
For another machine or several GPU generations, set an explicit supported list,
for example `-DCMAKE_CUDA_ARCHITECTURES="80;86;90"`. More images add compilation
work. Use this option rather than injecting `-gencode` flags: ARCH records the
same image list for its startup checks. Release also uses `-march=native` for
CPU code, so rebuild for a different CPU architecture rather than assuming a
locally optimized executable is portable.

## Build tests only when you need them

`BUILD_TESTING=OFF` in both Release presets leaves the regression suite out of
the normal application workflow without disabling simulation features.
`BUILD_TESTING=ON` registers the applicable test targets during configuration;
configuration itself does not compile them. The default `all` build then
compiles extra standalone test executables, increasing time and host-memory
pressure. CUDA-enabled test builds add further NVCC work on top of the CUDA
application build.

Even with tests enabled, `--target ARCH` builds only the application and its
dependencies, not the standalone tests. Select a specific test target when
you only need a focused check. For example, enable and run one small host
contract in the existing CPU build:

```bash
cmake -S . -B build-cpu -DBUILD_TESTING=ON
cmake --build build-cpu --target arch_boundary_plan --parallel 1
ctest --test-dir build-cpu -R '^boundary_plan$' --output-on-failure
```

CTest runs tests; it does not compile missing test executables. A build without
`--target ARCH` builds the default target set, which is larger when tests are
enabled. Choose further checks from [Tests](../../tests/README.md), and use the
module recipes in [Validation](../../validation/README.md) for scientific runs.
