# Building ARCH

[中文](Build.zh-CN.md) · [Quick start](../../README.md#build) · [Studio](Studio.md)

ARCH uses one CMake project for Linux, WSL2 and native Windows 11 x64. **Linux is recommended for scientific work.** Windows provides the same CPU, CUDA and Studio features within the documented [feature scope](../Features.md), with newer platform support that may still have instability or bugs.

## From source to CUDA and Studio

With the [platform tools](#tools-and-dependencies) installed, run these four commands from the repository root.

Linux / WSL2:

```bash
cmake -DARCH_PROFILE=linux-desktop-with-cuda -P cmake/dependencies/Prepare.cmake
cmake --preset linux-desktop-with-cuda
cmake --build --preset linux-desktop-with-cuda
build/linux-desktop-with-cuda/bin/arch-studio
```

Windows, in an **MSVC 14.44 x64 Developer Command Prompt** with native Node 24+ and GNU objdump available:

```bat
cmake -DARCH_PROFILE=windows-desktop-with-cuda -P cmake/dependencies/Prepare.cmake
cmake --preset windows-desktop-with-cuda
cmake --build --preset windows-desktop-with-cuda
build\windows-desktop-with-cuda\bin\arch-studio.cmd
```

Studio opens an empty project-selection page. Select your project, its build and executable, then a registered model and `.par`; see the [illustrated guide](Studio.md). Both builds provide CPU and CUDA execution. The parameter file's `compute_backend = cpu`, `cuda` or `auto` chooses the backend at runtime.

| Step | What it does | When to repeat it |
| --- | --- | --- |
| Prepare | Selects native tools, validates an existing HDF5 C++/HL installation or builds matching HDF5, and records local choices | First setup or when changing tools/dependency locations |
| Configure | Reads the selected profile, supplies HighFive/KLU through the existing dependency setup and generates build instructions | After changing CMake options |
| Build | Compiles Core and, for a desktop profile, Studio and its native helpers | After source changes |
| Launch | Opens Studio; Core can also run directly from `bin/ARCH` or `bin/ARCH.exe` | Each session |

The initial preparation/configuration downloads dependencies when needed. Repeated builds reuse them. Build directories and compiler ABIs stay separate; source-checkout Studio dependencies also belong to their host OS.

<a id="cpu-build"></a>
<a id="cuda-build"></a>
<a id="native-windows-cpu-and-cuda"></a>
## Choose a profile

Use the same profile name in the prepare, configure and build commands. Each writes to `build/<profile>/`, with executables under `bin/`.

| Contents | Linux / WSL2 | Windows 11 x64 |
| --- | --- | --- |
| CPU Core | `linux-core` | `windows-core` |
| CPU + CUDA Core | `linux-core-with-cuda` | `windows-core-with-cuda` |
| CPU Core + Studio | `linux-desktop` | `windows-desktop` |
| CPU + CUDA Core + Studio | `linux-desktop-with-cuda` | `windows-desktop-with-cuda` |

Core-only builds require neither Node nor objdump. Enabling Studio in the same build directory requires the desktop dependencies. Desktop profiles include the window and its tools by default; `with-cuda` adds CUDA to a program that also retains CPU execution. The [README](../../README.md#build) gives Core-only commands. To add Studio to an existing Core tree, use [the desktop section](#build-and-launch-the-desktop).

<a id="presets-and-build-options"></a>
All eight profiles use Release and Ninja. OpenMP and KLU are enabled, tests are off, and the build uses one job by default. CUDA profiles target the GPU visible during configuration. GNU profiles retain build-input evidence for later use in Studio. `cmake --list-presets` lists these and the direct-configuration presets described under [advanced configuration](#advanced-configuration).

## Tools and dependencies

| Component | Linux / WSL2 | Native Windows 11 x64 |
| --- | --- | --- |
| CPU compiler | C++20 GCC; manual builds can also select Clang | MSYS2 **UCRT64** GCC and its libgomp runtime |
| CUDA compiler | NVCC with a compatible GCC host | NVCC, MSVC 14.44 x64, Visual Studio C++ common tools and Windows SDK |
| Build tools | Git, Ninja, CMake 3.22+ for CPU or 3.25.2+ for CUDA | Native Git/Ninja; UCRT64 CMake for GCC or complete Kitware Windows CMake for MSVC |
| Required libraries | HDF5 C++/HL with zlib and OpenMP | The same libraries built for the selected compiler ABI |
| Shared headers / sparse CPU solver | HighFive 2.9.0 and KLU; SuiteSparse 7.13.0 provides the source fallback | The same versions, with separate GCC and MSVC library builds |
| CUDA runtime | CUDA Toolkit 12.0+ and compatible NVIDIA driver | Windows CUDA Toolkit and compatible NVIDIA driver |
| Sparse CUDA burning | Optional cuDSS **0.8**, matching the CUDA major version | cuDSS 0.8 import library/DLL and matching cuBLAS/cuBLASLt DLLs |
| Optional desktop | Native x64 Node 24+, npm, graphical session and Electron libraries | Native x64 Node 24+, npm and **GNU objdump** |
| Tests | Python 3.10+; NumPy and h5py for scientific checks | Native Python 3.10+; NumPy and h5py for scientific checks |

Preparation checks HDF5 by compiling and running a C++/HL/zlib probe. When an installation is unavailable, it builds HDF5 1.14.6 with zlib 1.3.1 in a compiler-specific prefix. HighFive and KLU retain their shared owner in [Dependencies.cmake](../../cmake/dependencies/Dependencies.cmake). `ARCH_ENABLE_OPENMP=OFF` disables CPU threading; `ARCH_ENABLE_KLU=OFF` removes the CPU sparse solver.

Tested combinations include Linux CUDA 12.3/GCC 12; Windows CPU GCC 16.2.0/CMake 4.4.4; and Windows CUDA 12.9.86 with MSVC 19.44.35229 (toolset 14.44), SDK 10.0.26100 and CMake 3.31.11. The Windows dependency builds use HDF5 1.14.6 and SuiteSparse 7.13.0. MSYS2 packages change with its rolling distribution.

### Linux / WSL2 installation

On Ubuntu 24.04:

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends build-essential cmake ninja-build git git-lfs libhdf5-dev
```

For CUDA, install NVIDIA's Linux CUDA Toolkit and a compatible host compiler. The preparation script prefers GCC 12 when available; [custom paths](#custom-tools-and-offline-preparation) can select another supported installation. Under WSL2, install the GPU driver on Windows and follow [NVIDIA's WSL guide](https://docs.nvidia.com/cuda/wsl-user-guide/index.html) for the Linux toolkit.

### Windows CPU installation

Install [MSYS2](https://www.msys2.org/docs/installer/) at a short local path such as `C:/msys64`. In its **UCRT64** terminal:

```bash
pacman -Syu
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libgomp mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

Reopen UCRT64 if the update requests it. Install native Git, then use **PowerShell** for the ARCH commands. Add the UCRT64 tools to that terminal before preparation, configuration and compilation:

```powershell
$env:Path = 'C:/msys64/ucrt64/bin;' + $env:Path
```

Preparation supplies matching HDF5; CMake supplies HighFive/KLU. For a different MSYS2 location, pass `-DARCH_UCRT_ROOT=C:/your/msys64/ucrt64` to preparation and put that directory's `bin` on PATH. Keep MSYS `usr/bin`, Anaconda libraries and other C++ runtimes out of this compiler environment.

### Windows CUDA installation

Install Visual Studio 2022 Build Tools with the C++ x64/x86 tools, **MSVC 14.44** and Windows SDK. Keep the VC common tools containing `VC/Auxiliary/Build/vcvarsall.bat`, which NVCC uses. Install a complete [Kitware Windows CMake](https://cmake.org/download/) distribution, native Ninja and the Windows CUDA Toolkit; MSVC resource tracking needs CMake's `cmcldeps.exe`.

Open an x64 **Developer Command Prompt** initialized for 14.44. For a custom installation, for example:

```bat
call "C:\Tools\VS2022\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -vcvars_ver=14.44
```

Make CMake, Ninja, Git and NVCC available in that terminal, then run the Windows CUDA profile. Preparation builds or selects MSVC-ABI HDF5 and records the matching compiler, SDK and OpenMP runtime directories. GCC/UCRT64 libraries use a separate ABI and prefix.

MSVC 14.38 fails an existing NaN contract check. NVIDIA's [CUDA 12.9 compiler table](https://docs.nvidia.com/cuda/archive/12.9.1/cuda-installation-guide-microsoft-windows/index.html) lists the 193x family; the 14.44 configuration above is ARCH's separately tested combination. Select another toolkit/toolset only after checking compatibility, without `--allow-unsupported-compiler`.

ARCH uses `/openmp:llvm /openmp:experimental` on MSVC. Preparation locates `libomp140.x86_64.dll` under the selected toolset's `Microsoft.VC143.OpenMP.LLVM` directory. This [Microsoft runtime](https://learn.microsoft.com/en-us/cpp/build/reference/openmp-enable-openmp-2-0-support?view=msvc-170) is nonredistributable and remains in the installed toolchain.

## Run and select paths

Linux / WSL2, using the CPU profile:

```bash
export OMP_NUM_THREADS=4
build/linux-core/bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

Windows Command Prompt; keep the initialized Developer environment for CUDA:

```bat
call build\windows-core\bin\arch-env.cmd
set OMP_NUM_THREADS=4
build\windows-core\bin\ARCH.exe Sod simulation/Sod/Sod_beginner.par
```

For CUDA, replace the profile in both paths with `linux-core-with-cuda` or `windows-core-with-cuda`, and edit the existing `compute_backend` assignment in a copy of the input. `cuda_device` selects the GPU ordinal. Core's `--check-run` and Studio's Run preparation show the selected backend and its reason; the Run log and `*_backend_plan.txt` record execution.

Windows configuration generates `bin/arch-env.cmd` from the selected dependency and compiler directories. Calling it sets the CLI environment and checks the required cuDSS DLLs when that provider is linked. Studio uses the same runtime data automatically.

**Relative paths inside `.par` resolve from the calculation's working directory, not the parameter file's directory.** Studio uses the project root; a CLI run uses the terminal's current directory. Prefer values such as `out_dir = output/first_sod`. `/mnt/c/src/ARCH` is a WSL path; native Windows uses `C:/src/ARCH`. Changing separators alone does not translate between them.

Save `.par` files as UTF-8. LF and CRLF are accepted. Windows executables carry a process-local UTF-8 manifest. Use short ASCII source/build paths for GNU LTO; ordinary local input/output paths support spaces and Unicode. UNC/network filesystems and arbitrary long paths are outside the tested configuration. Studio's project-relative fields use `/` on both platforms; see [Studio paths](Studio.md#paths-on-linux-and-windows).

## Studio desktop

<a id="linux-studio-desktop"></a>
Install native x64 Node **24+** with npm. Linux / WSL2 also needs a graphical session, WSLg when applicable, and Electron's system libraries. On Ubuntu 24.04:

```bash
sudo apt-get install --no-install-recommends libgtk-3-0t64 libnss3 libgbm1 libasound2t64 libx11-xcb1 libxkbcommon0 xterm util-linux
```

Run/Restart opens an independent terminal: `/usr/bin/xterm` with `/usr/bin/flock` on Linux, or the native Windows terminal. Editing and Preview can be used without an independent Run terminal.

Windows Studio needs GNU `objdump.exe` for build-input inspection. GCC/UCRT64 includes it; an MSVC/CUDA desktop can install only MSYS2's `mingw-w64-ucrt-x86_64-binutils`. This does not require building a GCC Core. The validated inspection format is GNU objdump; LLVM objdump and MSVC dumpbin are different interfaces. Use `ARCH_STUDIO_OBJDUMP` to select an absolute GNU objdump path if needed.

### Build and launch the desktop

To add Studio to an existing Core profile, enable the option in **that same tree**:

Linux / WSL2:

```bash
cmake -S . -B build/linux-core -DARCH_BUILD_STUDIO=ON
cmake --build build/linux-core --target arch-studio
build/linux-core/bin/arch-studio
```

Windows PowerShell, using the Core compiler environment:

```powershell
cmake -S . -B build/windows-core -DARCH_BUILD_STUDIO=ON
cmake --build build/windows-core --target arch-studio
& .\build\windows-core\bin\arch-studio.cmd
```

Use the existing `*-core-with-cuda` directory for a CUDA build. For Windows CUDA, start PowerShell from its initialized Developer terminal. `arch-studio` builds Core, assets, the locked Electron runtime and the two Windows helpers it needs. It reuses npm dependencies after ordinary source edits and repairs a removed `node_modules` directory. Windows invokes npm through Node, independently of PowerShell's `npm.ps1` policy.

The Core profiles retain GNU build-input evidence. Adding Studio to an arbitrary manual GNU tree also needs the [two evidence options](#advanced-configuration). Launch without arguments for the empty start page, or use the [model/configuration CLI options](Studio.md#launch-with-a-model-and-parameter-file).

### Windows tool configuration

The generated source launcher automatically selects `build/<profile>/studio/windows-tools.json`. This local file records CMake/Ninja/resource compiler, GNU objdump, native helpers, DLL paths and MSVC `INCLUDE`/`LIB`/`LIBPATH`. Required linked cuDSS DLLs are checked when opening a project. No project is opened or Core library checked by the empty start page.

For a custom setup or portable package, set `ARCH_STUDIO_WINDOWS_TOOLS_FILE` to that trusted local JSON file. An explicit `ARCH_STUDIO_WINDOWS_TOOLS` JSON value takes precedence. These settings belong to the launch environment, not the `.par` or workbench fields. Configured absolute machine paths stay outside portable packages.

### Package the desktop

Build a portable directory on its target OS, using the configured desktop tree:

```text
cmake --build build/linux-desktop-with-cuda --target arch-studio-package
```

On Windows, use `build/windows-desktop-with-cuda` from the matching compiler environment. Output is under `studio/.local/desktop-release/`:

| System | Directory | Entry |
| --- | --- | --- |
| Linux | `arch-studio-bin-linux-x64` | `arch-studio` |
| Windows | `arch-studio-bin-win32-x64` | `arch-studio-bin.exe` |

Keep the directory intact. It includes Electron, standalone Host Node, h5wasm and licenses. Core, compiler/runtime dependencies and the independent Run terminal remain external. For Windows, select the local tool configuration described above. Source launchers under a build's `bin/` instead use their checkout.

The packager reads Electron's exact version from `package.json` and checks the lockfile and installed package. `electron_config_cache` can select official checksum-matching cached archives. Preserve Node, Electron/Chromium and h5wasm/HDF5 licenses. Signing, installers and automatic updates are not configured.

## Custom tools and offline preparation

Pass tool or dependency choices to preparation, before `-P`, then configure normally. For example:

```bash
cmake -DARCH_PROFILE=linux-core-with-cuda \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-12 -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 \
  -DHDF5_ROOT=/opt/arch-deps -P cmake/dependencies/Prepare.cmake
cmake --preset linux-core-with-cuda
```

| Preparation option | Purpose |
| --- | --- |
| `CMAKE_C_COMPILER`, `CMAKE_CXX_COMPILER`, `CMAKE_CUDA_COMPILER` | Select a matching native toolchain; Linux NVCC uses the selected C++ host |
| `CMAKE_MAKE_PROGRAM`, `CMAKE_RC_COMPILER` | Select Ninja and the Windows resource compiler |
| `ARCH_UCRT_ROOT` | UCRT64 installation, default `C:/msys64/ucrt64` |
| `ARCH_STUDIO_NODE`, `ARCH_STUDIO_OBJDUMP` | Desktop runtime and Windows GNU PE inspector |
| `HDF5_ROOT`, `HDF5_DIR`, `KLU_DIR`, `CMAKE_PREFIX_PATH` | Reuse compatible installed dependencies |
| `CUDAToolkit_ROOT`, `CUDSS_ROOT` | Select CUDA and optional cuDSS installations |
| `ARCH_DEPENDENCY_ROOT` | Download, source and ABI-specific build cache; default `build/dependencies` |
| `ARCH_PREPARE_JOBS` | HDF5 dependency-build concurrency; default 2 |
| `ARCH_HDF5_SOURCE_DIR`, `ARCH_ZLIB_ARCHIVE` | Existing HDF5 1.14.6 source and pinned zlib 1.3.1 archive |
| `FETCHCONTENT_SOURCE_DIR_HIGHFIVE`, `FETCHCONTENT_SOURCE_DIR_SUITESPARSE` | Existing source trees for the normal CMake dependency owner |
| `ARCH_PREPARE_OFFLINE=ON` | Use existing dependency installations, sources and archives |

An explicit HDF5 prefix must pass the native C++/HL/zlib probe. Preparation writes `build/<profile>/prepared-toolchain.cmake`; regenerate it when selections change. A compiler/ABI change needs a fresh profile build directory. Offline preparation requires local HighFive and either installed KLU or local SuiteSparse; EOS tables are handled [separately](#offline-tables-and-lfs-bandwidth).

## cuDSS and sparse burning

cuDSS **0.8** is optional and is discovered when `ARCH_ENABLE_CUDSS=ON`. Select an installation with `CUDSS_ROOT`, containing `include/cudss.h` and its matching library; Windows also requires the import library, `bin/cudss64_0.dll`, and CUDA's cuBLAS/cuBLASLt DLLs. Configuration records these Windows DLLs in the shared runtime environment.

| Requested route | When cuDSS is unavailable |
| --- | --- |
| Ordinary CUDA hydrodynamics and compact DenseLU burning | Remain available |
| Sparse burning with explicit CUDA execution or explicit `cudss` solver | Rejected during Run preparation |
| Sparse burning with `compute_backend = auto` and automatic solver selection | Can select CPU KLU when its compiled route is available |

Core and Studio use the same Run checks and report the actual backend and selection reason. Keep KLU enabled for CPU sparse burning, and compile the needed network/EOS route; see [generated networks](../../validation/network/README.md). Linux loads vendor libraries only when a selected route needs them; unrelated Preview does not preload them.

For a distribution that promises cuDSS support, require it and run its four GPU checks with their existing numerical thresholds:

```bash
cmake --preset linux-core-with-cuda -DARCH_REQUIRE_CUDSS=ON -DBUILD_TESTING=ON
cmake --build build/linux-core-with-cuda --target arch-check-cudss
```

Use the corresponding Windows profile and directory on Windows. `ARCH_REQUIRE_CUDSS=ON` makes a missing provider a configuration error. `arch-check-cudss` builds and runs the checks; an unavailable GPU is a failed verification.

## Advanced configuration

The preparation profiles are conveniences around the same CMake project. A manually managed environment can configure it directly, for example:

```bash
cmake -S . -B build/custom -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DARCH_ENABLE_CUDA=OFF -DCMAKE_PREFIX_PATH=/opt/arch-deps \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build/custom/bin"
cmake --build build/custom --target ARCH --parallel 1
```

Select matching C/C++ compilers on the first configure. On Windows CPU, set `CMAKE_C_COMPILER`, `CMAKE_CXX_COMPILER` and `CMAKE_RC_COMPILER` to UCRT64 `gcc.exe`, `g++.exe` and `windres.exe`. For Windows CUDA, use `cl.exe`, SDK `rc.exe` and NVCC from the initialized MSVC 14.44 terminal, enable `ARCH_ENABLE_CUDA=ON`, and supply MSVC-ABI dependencies. `ARCH_RUNTIME_OUTPUT_DIRECTORY` controls the executable location; its direct-CMake default is the source tree's `bin/`.

To add Studio with build-input evidence to a manual GNU tree:

```text
cmake -S . -B build/custom -DARCH_BUILD_STUDIO=ON -DARCH_EMIT_LINK_DEPENDENCIES=ON -DARCH_RETAIN_LTO_LINK_INPUTS=ON
cmake --build build/custom --target arch-studio
```

These options retain linker/LTO inputs for freshness checks without changing IPO or floating-point settings. MSVC builds use their existing evidence path and do not enable the GNU LTO option. Explicit user settings remain in the cache.

The direct presets `cpu-release`, `cuda-release`, `studio-cpu-release` and `cuda-debug` remain available for manually prepared environments, using `build-cpu`, `build-cuda`, `build-studio-cpu` and `build-cuda-debug`. They do not run dependency preparation; `cuda-debug` enables tests. To tune the prepared profiles, add ordinary CMake `-D` options to configuration, or use `CMakeUserPresets.json` for local choices.

## Source archives and EOS tables

Sod uses IdealGas and requires no EOS table. Helmholtz and tabular EOS cases need the corresponding files under `EOS_toolkit/tables/`. Files beginning with `version https://git-lfs.github.com/spec/v1` are Git LFS pointers rather than table data.

In a Git checkout, request only the needed tables. The full Runtime installation includes these three:

```bash
git lfs pull --include="EOS_toolkit/tables/helmholtz/helm_table.dat,EOS_toolkit/tables/baryon/eos2.tab,EOS_toolkit/tables/baryon/eos4.tab" --exclude=""
```

This Git command is the same in PowerShell. Check each table's size and SHA-256 against the selected revision's pointer (`git show HEAD:EOS_toolkit/tables/helmholtz/helm_table.dat`) and retain its [attribution](../../THIRD_PARTY_NOTICES.md).

A source ZIP/tar has no Git metadata. Copy verified tables from the same tag/commit, or obtain them through a temporary checkout. Replace `YOUR_RELEASE_TAG_OR_COMMIT` with the archive's version.

Linux / WSL2:

```bash
git clone --no-checkout https://github.com/Shiro-Akane/ARCH.git ../ARCH-tables
GIT_LFS_SKIP_SMUDGE=1 git -C ../ARCH-tables checkout --detach YOUR_RELEASE_TAG_OR_COMMIT
git -C ../ARCH-tables lfs pull --include="EOS_toolkit/tables/helmholtz/helm_table.dat" --exclude=""
cp ../ARCH-tables/EOS_toolkit/tables/helmholtz/helm_table.dat EOS_toolkit/tables/helmholtz/
```

Windows PowerShell:

```powershell
git clone --no-checkout https://github.com/Shiro-Akane/ARCH.git ../ARCH-tables
$env:GIT_LFS_SKIP_SMUDGE = '1'
git -C ../ARCH-tables checkout --detach YOUR_RELEASE_TAG_OR_COMMIT
Remove-Item Env:GIT_LFS_SKIP_SMUDGE
git -C ../ARCH-tables lfs pull --include="EOS_toolkit/tables/helmholtz/helm_table.dat" --exclude=""
Copy-Item ../ARCH-tables/EOS_toolkit/tables/helmholtz/helm_table.dat EOS_toolkit/tables/helmholtz/
```

Select the baryon table paths in the same way when needed. The temporary checkout may be removed after copying.

## Offline tables and LFS bandwidth

Keep verified table copies outside build/output directories and the Git LFS cache. When the required LFS objects are already local, `git lfs checkout <path>` materializes them without downloading. To disable automatic downloads for this repository and its linked worktrees:

```bash
git config --local filter.lfs.smudge 'git-lfs smudge --skip -- %f'
git config --local filter.lfs.process 'git-lfs filter-process --skip'
git lfs checkout EOS_toolkit/tables/helmholtz/helm_table.dat
```

These commands also work in PowerShell. Download missing objects with the scoped `git lfs pull --include=... --exclude=""` command above.

## Install a runtime package

After building the selected profile, materialize all three EOS tables listed above, then install the Runtime component.

Linux / WSL2:

```bash
cmake --install build/linux-core --prefix "$PWD/arch-install" --component Runtime
cd arch-install/share/arch
../../bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

Windows Command Prompt:

```bat
call build\windows-core\bin\arch-env.cmd
cmake --install build/windows-core --prefix "%CD%/arch-install" --component Runtime
cd arch-install\share\arch
..\..\bin\ARCH.exe Sod simulation/Sod/Sod_beginner.par
```

Use the CUDA build directory for that backend. The installation includes ARCH, sample inputs, EOS tables, licenses and Windows helpers. Keep the selected HDF5, OpenMP and CUDA/cuDSS runtime libraries available. Relative input paths continue to use the launch working directory.

## Parallel compilation and memory

Start with `ARCH_CUDA_HEAVY_COMPILE_JOBS=1` and `--parallel 1`. Ninja enforces the heavy CUDA/dispatch job pool as well as the total job limit. Other generators do not use this job pool. CUDA compilation consumes host RAM and CPU; free GPU memory does not reduce those requirements. Larger generated networks and additional GPU architectures increase compile work.

On Linux / WSL2, the memory guard can stop the owned build before sustained memory pressure:

```bash
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build/linux-core-with-cuda --target ARCH --parallel 1
```

It checks available RAM, swap growth and Linux PSI full-stall counters. Defaults stop at less than 1.5 GiB available RAM, more than 256 MiB additional swap, or 20% memory / 50% I/O full-stall sustained for 10 seconds. It needs `/proc`, PSI, Python `os.pidfd_open`/`signal.pidfd_send_signal` and Linux child-subreaper support. `--log <new-file>` before `--` saves measurements. `Ctrl+C` stops the build's owned descendants.

On native Windows, use the conservative job limits and monitor available RAM in Task Manager; the Linux guard does not run there. Increase parallelism only after measuring headroom. Under WSL, `free -h` shows the memory available to Linux, which can be smaller than installed system RAM.

## Optimization, compilers and target GPUs

Release builds use the selected compiler's optimization and supported LTO/IPO. GNU builds use `-O3 -march=native`; MSVC uses its Release optimization with `/fp:strict`. The shared floating-point settings preserve finite checks and explicit operation order, disable fast-math and implicit contraction, and keep CUDA subnormal handling. Build parallelism can be adjusted without changing these settings.

`CMAKE_CUDA_ARCHITECTURES=native` targets the GPU visible during configuration. For another machine, provide a supported image list such as `-DCMAKE_CUDA_ARCHITECTURES="80;86;90"`. ARCH uses the same list for startup checks. More images take longer to compile. GNU CPU code also targets the build machine through `-march=native`, so rebuild when moving to a different CPU architecture.

CMake uses `ccache` when available. A warm cache can shorten repeated builds; edits to widely included headers still affect many translation units.

## Build output

Configuration reports the selected backend, optimization and libraries. To see detailed dependency messages, append `--log-level=VERBOSE` or set `ARCH_VERBOSE_BUILD=ON`. To see compiler/linker commands, run `cmake --build <build-directory> --verbose`. Debug controls compilation settings, not log verbosity.

## Build tests only when you need them

Set `BUILD_TESTING=ON` to register test targets. CTest executes them after they have been built. A focused example, on either OS, is:

```bash
cmake -S . -B build/linux-core -DBUILD_TESTING=ON
cmake --build build/linux-core --target arch_boundary_plan --parallel 1
ctest --test-dir build/linux-core -R '^boundary_plan$' --output-on-failure
```

Replace `build/linux-core` with the selected Windows build directory and keep its compiler environment. Provide `-DPython3_EXECUTABLE=<absolute-path>` when selecting a specific Python. Scientific Python checks need NumPy and h5py. CUDA-enabled test builds add NVCC work; `--target ARCH` builds the application without the standalone tests.

Linux tooling checks additionally use `mold`, `shellcheck` and a Python with pidfd support. On Ubuntu 24.04:

```bash
sudo apt-get install --no-install-recommends python3-numpy python3-h5py mold shellcheck
python3 -m unittest discover -s tests/tooling -p 'test_*.py' -v
```

Use [Tests](../../tests/README.md) for target names and platform groups, and [Validation](../../validation/README.md) for scientific reproduction. Case-specific `physical_boundary.cpp` and `gravity_boundary.cpp` compile alongside their registered case; see [user boundaries](UserBoundaries.md).

## Troubleshooting

| Symptom | Check |
| --- | --- |
| CMake selects another compiler/library | Start a fresh build directory with explicit compiler paths and the matching HDF5/KLU prefix |
| Windows command-chain or resource compilation failure | Keep native backslashes in `ComSpec`, `.EXE` in `PATHEXT`, and the selected `rc.exe`/`windres.exe` on PATH |
| MSVC resource dependency error | Use complete Kitware CMake, including `cmcldeps.exe` |
| Missing Windows DLL | Call the selected build's `arch-env.cmd` for CLI use, or regenerate the Studio tool configuration after moving dependencies |
| GNU LTO fails in a Unicode build path | Move source/build/retained temporary directories to a short ASCII path |
| Electron fails to open on Linux | Check the graphical session/WSLg, `DISPLAY`, and missing entries in `ldd studio/node_modules/electron/dist/electron` |
| Windows Run cannot open a terminal | Check the native terminal; a custom Windows Terminal alias can be set through the trusted tool configuration |
| Studio reports build freshness unknown | Full dependency coverage is unavailable for some toolchains, including MSVC/CUDA; keep the explicit build selection and inspect Build diagnostics |
| Preview exceeds a resource limit | Reduce preview sampling or initial AMR size; Windows uses a 1024 MiB committed-memory limit and user CPU time, while Linux uses address-space and process CPU limits |
| EOS table contains a short Git LFS pointer | Materialize the matching Git LFS table before using that EOS |

The UCRT64/GCC 16.2.0 Plotfile test entry has a compiler-specific IPO workaround in CMake; production targets retain IPO. EOS input parsing also follows the host library's representable range: MSVC rejects tested literals below binary64 range such as `1e-500`, while the Linux extractor accepts signed zero. Use the supplied canonical tables and representable values.
