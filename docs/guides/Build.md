# Building ARCH

[中文](Build.zh-CN.md) · [Quick start](../../README.md#build) · [Studio](Studio.md)

ARCH uses one CMake project for Linux, WSL2 and native Windows 11 x64. **Linux is recommended for scientific work.** Windows provides the same CPU, CUDA and Studio features within the documented [feature scope](../Features.md), with newer platform support that may still have instability or bugs. Studio is optional; the command-line application runs without Node or Electron.

Follow the CPU or CUDA route below, then add Studio if needed. Linux examples use Bash; Windows examples use PowerShell unless marked **Developer Command Prompt**. Run project commands from the ARCH root. Keep separate build directories and dependency prefixes for each operating system and compiler ABI.

## Tools and dependencies

| Component | Linux / WSL2 | Native Windows 11 x64 |
| --- | --- | --- |
| CPU compiler | C++20 GCC or Clang; matching C/C++ toolchain | MSYS2 **UCRT64** GCC and its libgomp runtime |
| CUDA compiler | NVCC with a compatible GCC/Clang host | NVCC with MSVC x64, Visual Studio C++ common tools and Windows SDK |
| Build tools | Git, Ninja, CMake 3.22+ for CPU or 3.25.2+ for CUDA | Native Git and Ninja; UCRT64 CMake for GCC or complete Kitware Windows CMake for MSVC |
| Required libraries | HDF5 C++/HL, OpenMP | HDF5 C++/HL and OpenMP built for the chosen compiler ABI |
| Shared headers / sparse CPU solver | HighFive 2.9.0; KLU from an installed package or SuiteSparse 7.13.0 | The same versions, with separate GCC and MSVC library builds |
| CUDA runtime | CUDA Toolkit 12.0+, compatible NVIDIA driver | Windows CUDA Toolkit and compatible NVIDIA driver |
| Sparse CUDA burning | Optional cuDSS **0.8**, matching the CUDA major version | cuDSS 0.8 DLL/import library plus matching cuBLAS/cuBLASLt DLLs |
| Optional desktop | Native x64 Node 24+, npm, Electron, graphical session | Native x64 Node 24+, npm, Electron, Windows desktop |
| Test tools | Python 3.10+; NumPy and h5py for scientific checks | Native Python 3.10+; NumPy and h5py for scientific checks |

HighFive and the SuiteSparse fallback are fetched on first configuration unless local source overrides are supplied. The versions are declared in [Dependencies.cmake](../../cmake/dependencies/Dependencies.cmake). OpenMP and KLU are enabled by default. `ARCH_ENABLE_OPENMP=OFF` disables CPU threading; `ARCH_ENABLE_KLU=OFF` removes the CPU sparse solver.

Tested toolchain combinations include Linux CUDA 12.3/GCC 12; Windows CPU GCC 16.2.0 with CMake 4.4.4; and Windows CUDA 12.9.86 with MSVC 19.44.35229 (toolset 14.44), SDK 10.0.26100 and CMake 3.31.11. The Windows library builds use HDF5 1.14.6 and SuiteSparse 7.13.0. MSYS2 is a rolling distribution, so its current packages may differ.

For the Windows CUDA recipe, use the tested MSVC 14.44 toolset: 14.38 fails an existing NaN contract check. NVIDIA's [CUDA 12.9 compiler table](https://docs.nvidia.com/cuda/archive/12.9.1/cuda-installation-guide-microsoft-windows/index.html) lists the 193x compiler family; the 14.44 combination above is ARCH's separately tested configuration. Check compiler compatibility when choosing a different toolkit/toolset, without using `--allow-unsupported-compiler`.

### Linux / WSL2 installation

On Ubuntu 24.04:

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends \
  build-essential cmake ninja-build git git-lfs libhdf5-dev
```

For CUDA, install NVIDIA's Linux CUDA Toolkit and use its compatible host compiler. Under WSL2, the GPU driver belongs on Windows; follow [NVIDIA's WSL guide](https://docs.nvidia.com/cuda/wsl-user-guide/index.html) for the toolkit inside WSL.

### Windows CPU installation

Install [MSYS2](https://www.msys2.org/docs/installer/) at a short local path such as `C:/msys64`. Open its **UCRT64** terminal and update/install the tools:

```bash
pacman -Syu
# Reopen UCRT64 if the update asks you to close the terminal.
pacman -Syu --needed mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-libgomp mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-zlib
```

Use native PowerShell for the remaining Windows commands. Install native Git, then prepare the upstream sources below. Adjust these example paths to your installation:

```powershell
$repo = 'C:/src/ARCH'
$ucrt = 'C:/msys64/ucrt64'
$sources = 'C:/src/arch-dependencies'
$deps = 'C:/tools/arch-ucrt-deps'
New-Item -ItemType Directory -Force $sources | Out-Null
git clone --depth 1 --branch hdf5_1.14.6 https://github.com/HDFGroup/hdf5.git "$sources/hdf5"
git clone --depth 1 --branch v2.9.0 https://github.com/BlueBrain/HighFive.git "$sources/highfive"
git clone --depth 1 --branch v7.13.0 https://github.com/DrTimothyAldenDavis/SuiteSparse.git "$sources/suitesparse"
$env:Path = "$deps/bin;$ucrt/bin;$env:SystemRoot\System32;$env:SystemRoot"
$env:ComSpec = Join-Path $env:SystemRoot 'System32\cmd.exe'
$env:PATHEXT = '.COM;.EXE;.BAT;.CMD'
$common = @('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
  "-DCMAKE_C_COMPILER:FILEPATH=$ucrt/bin/gcc.exe",
  "-DCMAKE_CXX_COMPILER:FILEPATH=$ucrt/bin/g++.exe",
  "-DCMAKE_MAKE_PROGRAM:FILEPATH=$ucrt/bin/ninja.exe",
  "-DCMAKE_INSTALL_PREFIX:PATH=$deps", '-DCMAKE_CXX_STANDARD=20',
  '-DCMAKE_POLICY_VERSION_MINIMUM=3.5')
```

Build HDF5 with C++/HL and zlib, and the minimal KLU dependency set, using that same compiler. Check each command's exit status before continuing (`$LASTEXITCODE` in PowerShell).

```powershell
cmake -S "$sources/hdf5" -B "$sources/build-hdf5-ucrt" @common `
  -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF -DBUILD_TESTING=OFF `
  -DHDF5_BUILD_CPP_LIB=ON -DHDF5_BUILD_HL_LIB=ON -DHDF5_BUILD_FORTRAN=OFF `
  -DHDF5_BUILD_TOOLS=OFF -DHDF5_BUILD_EXAMPLES=OFF -DHDF5_ENABLE_PARALLEL=OFF `
  -DHDF5_ENABLE_Z_LIB_SUPPORT=ON -DHDF5_ENABLE_SZIP_SUPPORT=OFF `
  -DHDF5_ENABLE_NONSTANDARD_FEATURE_FLOAT16=OFF "-DZLIB_ROOT:PATH=$ucrt"
cmake --build "$sources/build-hdf5-ucrt" --parallel 2
cmake --install "$sources/build-hdf5-ucrt"

cmake -S "$sources/suitesparse" -B "$sources/build-klu-ucrt" @common `
  '-DSUITESPARSE_ENABLE_PROJECTS=klu;amd;colamd' -DKLU_USE_CHOLMOD=OFF `
  -DSUITESPARSE_USE_CUDA=OFF -DSUITESPARSE_USE_FORTRAN=OFF -DSUITESPARSE_REQUIRE_BLAS=OFF `
  -DBLA_VENDOR=Generic -DSUITESPARSE_USE_OPENMP=OFF -DSUITESPARSE_USE_PYTHON=OFF `
  -DSUITESPARSE_DEMOS=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON
cmake --build "$sources/build-klu-ucrt" --parallel 2
cmake --install "$sources/build-klu-ucrt"
```

HighFive is header-only; ARCH can use the checked-out source directly. HDF5's optional `_Float16` extension is disabled because this GCC configuration lacks `FLT16_MAX`. The CMake policy setting supports the pinned dependencies with CMake 4. Keep MSYS `usr/bin`, Anaconda libraries and other C++ runtimes out of this build's PATH.

### Windows CUDA installation

Install Visual Studio 2022 Build Tools with the C++ x64/x86 tools, MSVC toolset 14.44 and Windows SDK. Keep the VC common tools containing `VC/Auxiliary/Build/vcvarsall.bat`, which NVCC uses. Install a complete [Kitware Windows CMake](https://cmake.org/download/) distribution, native Ninja and the matching Windows CUDA Toolkit. MSVC resource tracking needs Kitware's `cmcldeps.exe`.

Use a separate prefix such as `C:/tools/arch-msvc-deps` for HDF5 and KLU. GCC/UCRT64 libraries cannot be reused here. The following **Developer Command Prompt** example initializes the selected toolset and builds HDF5 from the source prepared above:

```bat
call "C:\Tools\VS2022\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -vcvars_ver=14.44
set "ARCH_CMAKE=C:\Tools\cmake"
set "ARCH_CUDA=C:\Tools\cuda-12.9"
set "ARCH_MSVC_DEPS=C:\Tools\arch-msvc-deps"
set "PATH=%ARCH_CMAKE%\bin;C:\Tools\ninja;%ARCH_CUDA%\bin;%ARCH_MSVC_DEPS%\bin;%PATH%"
cmake -S C:/src/arch-dependencies/hdf5 -B C:/src/arch-dependencies/build-hdf5-msvc -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe ^
  "-DCMAKE_INSTALL_PREFIX=%ARCH_MSVC_DEPS%" ^
  -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF -DBUILD_TESTING=OFF ^
  -DHDF5_BUILD_CPP_LIB=ON -DHDF5_BUILD_HL_LIB=ON -DHDF5_BUILD_FORTRAN=OFF ^
  -DHDF5_BUILD_TOOLS=OFF -DHDF5_BUILD_EXAMPLES=OFF -DHDF5_ENABLE_PARALLEL=OFF ^
  -DHDF5_ENABLE_Z_LIB_SUPPORT=OFF -DHDF5_ENABLE_SZIP_SUPPORT=OFF ^
  -DHDF5_ENABLE_NONSTANDARD_FEATURE_FLOAT16=OFF
cmake --build C:/src/arch-dependencies/build-hdf5-msvc --parallel 2
cmake --install C:/src/arch-dependencies/build-hdf5-msvc
```

ARCH's normal SuiteSparse fallback builds KLU with the same MSVC compiler. Keep Git available in this Developer terminal for dependency fetching. An installed MSVC KLU package may be used instead through `CMAKE_PREFIX_PATH`.

ARCH uses `/openmp:llvm /openmp:experimental` on MSVC. Add the selected toolset's `Microsoft.VC143.OpenMP.LLVM` directory containing `libomp140.x86_64.dll` to the runtime PATH. It is usually under `VC/Redist/MSVC/<installed-version>/debug_nonredist/x64/`. This [Microsoft OpenMP runtime](https://learn.microsoft.com/en-us/cpp/build/reference/openmp-enable-openmp-2-0-support?view=msvc-170) is nonredistributable; it stays in the user's toolchain installation.

## Configure first, then compile

Configuration locates the compiler and libraries, writes `CMakeCache.txt` and generates Ninja instructions. Compilation then builds the application. Repeat the build command after editing source; reconfigure when changing options. Select a new build directory when changing operating systems, compilers or generators.

### CPU build

Linux / WSL2:

```bash
cmake --preset cpu-release
cmake --build build-cpu --target ARCH --parallel 1
```

Windows PowerShell, using the UCRT64 variables and PATH above:

```powershell
Set-Location $repo
$build = "$repo/build-windows-cpu"
cmake -S . -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_C_COMPILER:FILEPATH=$ucrt/bin/gcc.exe" `
  "-DCMAKE_CXX_COMPILER:FILEPATH=$ucrt/bin/g++.exe" `
  "-DCMAKE_RC_COMPILER:FILEPATH=$ucrt/bin/windres.exe" `
  "-DCMAKE_PREFIX_PATH:PATH=$deps" "-DHDF5_ROOT:PATH=$deps" `
  "-DFETCHCONTENT_SOURCE_DIR_HIGHFIVE:PATH=$sources/highfive" `
  -DARCH_ENABLE_CUDA=OFF -DARCH_ENABLE_OPENMP=ON `
  -DARCH_ENABLE_KLU=ON -DARCH_FETCH_SUITESPARSE=OFF `
  -DARCH_BUILD_STUDIO=OFF -DBUILD_TESTING=OFF `
  "-DARCH_RUNTIME_OUTPUT_DIRECTORY:PATH=$build/bin"
cmake --build $build --target ARCH --parallel 1
```

The resulting executables are `build-cpu/bin/ARCH` and `build-windows-cpu/bin/ARCH.exe`. `ARCH_RUNTIME_OUTPUT_DIRECTORY` controls that location; without it, the executable goes to the source tree's `bin/` directory.

<a id="native-windows-cpu-and-cuda"></a>
### CUDA build

Linux / WSL2:

```bash
cmake --preset cuda-release
cmake --build build-cuda --target ARCH --parallel 1
```

Use matching GCC major versions for C and C++; select the CUDA host compiler from that same installation. Set compiler paths on the first configuration, for example `-DCMAKE_C_COMPILER=/usr/bin/gcc-12 -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-12`.

Windows **Developer Command Prompt**, after the environment and HDF5 setup above:

```bat
cd /d C:\src\ARCH
cmake -S . -B build-cuda-win -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe ^
  "-DCMAKE_CUDA_COMPILER=%ARCH_CUDA%/bin/nvcc.exe" ^
  "-DCMAKE_RC_COMPILER=C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe" ^
  "-DCUDAToolkit_ROOT=%ARCH_CUDA%" -DCMAKE_CUDA_ARCHITECTURES=native ^
  "-DCMAKE_PREFIX_PATH=%ARCH_MSVC_DEPS%" "-DHDF5_ROOT=%ARCH_MSVC_DEPS%" ^
  "-DFETCHCONTENT_SOURCE_DIR_HIGHFIVE=C:/src/arch-dependencies/highfive" ^
  -DARCH_ENABLE_CUDA=ON -DARCH_ENABLE_OPENMP=ON ^
  -DARCH_CUDA_HEAVY_COMPILE_JOBS=1 -DBUILD_TESTING=OFF ^
  "-DARCH_RUNTIME_OUTPUT_DIRECTORY=%CD%/build-cuda-win/bin"
cmake --build build-cuda-win --target ARCH --parallel 1
```

Adjust the SDK path to the installed version. Native CUDA uses shared `cudart`. Runtime PATH must contain its CUDA DLLs, matching HDF5, the installed LLVM OpenMP runtime and cuDSS/cuBLAS when used.

The Linux CUDA preset writes `build-cuda/bin/ARCH`; the Windows example writes `build-cuda-win/bin/ARCH.exe`. Both executables can run CPU or CUDA cases. `compute_backend = cpu`, `cuda` or `auto` selects the backend when a simulation starts. A CPU-only build cannot enable CUDA through a parameter change. GPU-target and memory options are described below.

### Presets and build options

`cmake --list-presets` lists the shared recipes in [CMakePresets.json](../../CMakePresets.json). Use them with the platform's compiler/dependency environment, or pass the explicit options above.

| Preset | Directory | Purpose |
| --- | --- | --- |
| `cpu-release` | `build-cpu` | CPU application |
| `studio-cpu-release` | `build-studio-cpu` | GNU CPU application and desktop, with build-input evidence |
| `cuda-release` | `build-cuda` | CPU/CUDA application targeting the visible GPU |
| `cuda-debug` | `build-cuda-debug` | CUDA development build with tests |

Release presets omit tests; `cuda-debug` enables them. All presets use Ninja and OpenMP. `studio-cpu-release` enables GNU linker evidence and LTO input retention; for an MSVC CUDA desktop, enable Studio in the existing CUDA tree as described below. `ARCH_BUILD_STUDIO` defaults to `OFF`.

## Run and select paths

Linux / WSL2:

```bash
export OMP_NUM_THREADS=4
./build-cpu/bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

Windows PowerShell:

```powershell
$env:OMP_NUM_THREADS = '4'
& .\build-windows-cpu\bin\ARCH.exe Sod simulation/Sod/Sod_beginner.par
```

For CUDA, substitute that build's executable and change the existing `compute_backend` assignment in a copy of the input. `cuda_device` selects the GPU ordinal. The Run log and `*_backend_plan.txt` report the selected backend.

The `.par` format is shared. Relative table, output and restart paths resolve against the **simulation's working directory**, not the parameter file's directory. Studio uses the project root. Prefer relative values such as `out_dir = output/first_sod`; absolute paths must match the executing OS. `/mnt/c/src/ARCH` is a WSL path, while `C:/src/ARCH` is a Windows path. Changing separators alone does not translate between them.

Save `.par` files as UTF-8. Windows executables include a process-local UTF-8 manifest, so changing the system locale is unnecessary. Use short ASCII source/build paths for GNU LTO. Spaces and Unicode in ordinary local input/output paths are supported; UNC/network filesystems and arbitrary long paths are not part of the tested configuration. Studio's project-relative fields use `/` on both platforms; see [Studio paths](Studio.md#paths-on-linux-and-windows).

## Studio desktop

<a id="linux-studio-desktop"></a>
Studio needs native x64 Node **24+** with npm and the locked Electron version. Use an official Node distribution or an existing Node manager. Retain the distribution's license for packaging. Node, Electron, build caches and `studio/node_modules` belong to their host OS; use separate checkouts when building the desktop on both systems.

Linux / WSL2 also needs a graphical session (WSLg under WSL) and Electron's shared libraries. On Ubuntu 24.04:

```bash
sudo apt-get install --no-install-recommends \
  libgtk-3-0t64 libnss3 libgbm1 libasound2t64 libx11-xcb1 libxkbcommon0 \
  xterm util-linux
```

Run/Restart opens an independent terminal: `/usr/bin/xterm` and `/usr/bin/flock` on Linux, or Windows Terminal on Windows. Editing and Preview do not require this terminal.

### Build and launch the desktop

Linux / WSL2, with native Node 24+ on PATH:

```bash
node --version
cmake --preset studio-cpu-release -DARCH_STUDIO_NODE="$(command -v node)"
cmake --build build-studio-cpu --parallel 1
build-studio-cpu/bin/arch-studio --project "$PWD" --build-dir build-studio-cpu \
  --binary build-studio-cpu/bin/ARCH --case Sod --config simulation/Sod/Sod_beginner.par
```

Windows: enable Studio in the CPU or CUDA build you configured above. In PowerShell, set `$build` and `$cmake` to that tree and toolchain; for CUDA, launch PowerShell from the initialized Developer Command Prompt to inherit its compiler environment.

```powershell
$build = 'C:/src/ARCH/build-windows-cpu'
$cmake = 'C:/msys64/ucrt64/bin/cmake.exe'
$node = 'C:/tools/node/node.exe'
& $cmake -S . -B $build -DARCH_BUILD_STUDIO=ON "-DARCH_STUDIO_NODE:FILEPATH=$node" `
  -DARCH_EMIT_LINK_DEPENDENCIES=ON -DARCH_RETAIN_LTO_LINK_INPUTS=ON
& $cmake --build $build --target arch-studio arch_windows_process arch_windows_build_publish --parallel 1
```

The two GNU options record linker inputs used by the CPU Studio Build checks. For CUDA, use the existing MSVC tree without these options:

```powershell
$build = 'C:/src/ARCH/build-cuda-win'
$cmake = 'C:/Tools/cmake/bin/cmake.exe'
$node = 'C:/tools/node/node.exe'
& $cmake -S . -B $build -DARCH_BUILD_STUDIO=ON "-DARCH_STUDIO_NODE:FILEPATH=$node"
& $cmake --build $build --target arch-studio arch_windows_process arch_windows_build_publish --parallel 1
```

The same targets build both desktops. `arch-studio-dependencies` checks the shared npm installation against the package files and Node runtime; it installs when these change or `node_modules` is removed. `arch-studio-assets` builds the UI, so ordinary source edits reuse installed dependencies. `arch-studio-runtime` prepares Electron. CMake invokes `npm-cli.js` on Windows, so PowerShell's `npm.ps1` execution policy does not affect these commands.

### Windows tool configuration

Set `ARCH_STUDIO_WINDOWS_TOOLS` in the launching shell so Studio can find the selected compiler tools, DLLs and native process/file helpers. Paths must be absolute local paths. The following is a complete GCC/UCRT64 example:

```powershell
$repo = 'C:/src/ARCH'
$build = "$repo/build-windows-cpu"
$ucrtBin = 'C:/msys64/ucrt64/bin'
$deps = 'C:/tools/arch-ucrt-deps'
$tools = @{
  cmake = "$ucrtBin/cmake.exe"
  supervisor = "$build/arch_windows_process.exe"
  ninja = "$ucrtBin/ninja.exe"
  objdump = "$ucrtBin/objdump.exe"
  publisher = "$build/arch_windows_build_publish.exe"
  rc = "$ucrtBin/windres.exe"
  terminal = (Join-Path $env:LOCALAPPDATA 'Microsoft/WindowsApps/wt.exe')
  path = "$deps/bin;$ucrtBin;$env:SystemRoot/System32;$env:SystemRoot"
}
$env:ARCH_STUDIO_WINDOWS_TOOLS = $tools | ConvertTo-Json -Depth 4 -Compress
& "$build/bin/arch-studio.cmd" --project $repo --build-dir build-windows-cpu `
  --binary build-windows-cpu/bin/ARCH.exe --case Sod --config simulation/Sod/Sod_beginner.par
```

Use the actual Windows Terminal alias location if different. These settings belong to the local launch environment, not to `.par` files. Source launchers select the configured Node; portable packages use their bundled Node unless `ARCH_STUDIO_NODE` explicitly selects another runtime.

For **MSVC/CUDA**, start PowerShell from the initialized Developer Command Prompt and replace the tool configuration with the following. Adjust every installation path, especially the OpenMP runtime directory and SDK `rc.exe`; the latter must match `CMAKE_RC_COMPILER` in the selected tree.

```powershell
$repo = 'C:/src/ARCH'
$build = "$repo/build-cuda-win"
$vc = Split-Path (Get-Command cl.exe).Source
$sdk = 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64'
$omp = 'C:/Tools/VS2022/VC/Redist/MSVC/14.44.35112/debug_nonredist/x64/Microsoft.VC143.OpenMP.LLVM'
$tools = @{
  cmake = 'C:/Tools/cmake/bin/cmake.exe'
  supervisor = "$build/arch_windows_process.exe"
  ninja = 'C:/Tools/ninja/ninja.exe'
  objdump = 'C:/msys64/ucrt64/bin/objdump.exe'
  publisher = "$build/arch_windows_build_publish.exe"
  rc = "$sdk/rc.exe"
  terminal = (Join-Path $env:LOCALAPPDATA 'Microsoft/WindowsApps/wt.exe')
  path = "$vc;$sdk;C:/Tools/arch-msvc-deps/bin;C:/Tools/cuda-12.9/bin;$omp;C:/msys64/ucrt64/bin;$env:SystemRoot/System32;$env:SystemRoot"
  compilerEnvironment = @{
    INCLUDE = (($env:INCLUDE -split ';' | Where-Object { $_ }) -join ';')
    LIB = (($env:LIB -split ';' | Where-Object { $_ }) -join ';')
    LIBPATH = (($env:LIBPATH -split ';' | Where-Object { $_ }) -join ';')
  }
}
$env:ARCH_STUDIO_WINDOWS_TOOLS = $tools | ConvertTo-Json -Depth 4 -Compress
& "$build/bin/arch-studio.cmd" --project $repo --build-dir build-cuda-win `
  --binary build-cuda-win/bin/ARCH.exe --case Sod --config simulation/Sod/Sod_beginner.par
```

Include cuDSS's `bin` directory in `tools.path` when using sparse CUDA burning. `objdump.exe` is the PE inspection utility; it does not select GCC as the CUDA host compiler. The compiler's `INCLUDE`, `LIB` and `LIBPATH` lists carry the MSVC header/library search paths.

### Package the desktop

Build the portable directory on its target operating system:

```bash
cmake --build build-studio-cpu --target arch-studio-package --parallel 1
```

On Windows, use the same command with `build-windows-cpu` or `build-cuda-win` from the corresponding compiler shell. Output is under `studio/.local/desktop-release/`:

| System | Directory | Entry |
| --- | --- | --- |
| Linux | `arch-studio-bin-linux-x64` | `arch-studio` |
| Windows | `arch-studio-bin-win32-x64` | `arch-studio-bin.exe` |

Keep the entire directory together. It includes Electron, standalone Host Node, h5wasm and licenses. Core, its compiler tools/runtime libraries and the independent terminal remain external. Set the Windows tool configuration before launching the portable entry too. The generated `bin/arch-studio` or `.cmd` source launcher instead depends on its checkout.

The packager reads Electron's exact version from `package.json` and checks the lockfile and installed package. `electron_config_cache` can select an offline cache populated with official, checksum-matching Electron archives. Preserve Node, Electron/Chromium and h5wasm/HDF5 license files. Packaging creates a portable directory; signing, installers and automatic updates are not configured.

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

After building ARCH, materialize all three tables listed above. On Windows, also build the native helpers with `cmake --build <build-directory> --target arch_windows_process arch_windows_build_publish`. Then install the Runtime component.

Linux / WSL2:

```bash
cmake --install build-cpu --prefix "$PWD/arch-install" --component Runtime
cd arch-install/share/arch
../../bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

Windows PowerShell:

```powershell
cmake --install build-windows-cpu --prefix "$PWD/arch-install" --component Runtime
Set-Location arch-install/share/arch
& ../../bin/ARCH.exe Sod simulation/Sod/Sod_beginner.par
```

Use the CUDA build directory for that backend. The installation includes ARCH, sample inputs, EOS tables, licenses and Windows helpers. HDF5, OpenMP and CUDA/cuDSS runtime libraries must remain available in the selected environment. Relative paths continue to use the launch working directory.

## cuDSS and sparse burning

`ARCH_ENABLE_CUDSS=ON` enables discovery of cuDSS **0.8** in CUDA builds. Install it under a prefix containing `include/cudss.h` and `lib/` or `lib64/`; Windows additionally needs its import library and `bin/cudss64_0.dll`. For example:

```bash
cmake -S . -B build-cuda -DCUDSS_ROOT="$HOME/.local/cudss"
```

Windows PowerShell, in the MSVC environment:

```powershell
cmake -S . -B build-cuda-win -DCUDSS_ROOT:PATH=C:/Tools/cudss-0.8
```

Add its `bin` directory to the runtime PATH on Windows, along with matching CUDA cuBLAS/cuBLASLt DLLs. CMake prints the selected library. If cuDSS is unavailable or `ARCH_ENABLE_CUDSS=OFF`, compact DenseLU burning and other CUDA features remain available, while routes requiring the sparse provider report an unsupported configuration. Keep KLU enabled for CPU sparse burning. The network/EOS route must also be compiled; see [generated networks](../../validation/network/README.md).

## Parallel compilation and memory

Start with `ARCH_CUDA_HEAVY_COMPILE_JOBS=1` and `--parallel 1`. Ninja enforces the heavy CUDA/dispatch job pool as well as the total job limit. Other generators do not use this job pool. CUDA compilation consumes host RAM and CPU; free GPU memory does not reduce those requirements. Larger generated networks and additional GPU architectures increase compile work.

On Linux / WSL2, the memory guard can stop the owned build before sustained memory pressure:

```bash
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-cuda --target ARCH --parallel 1
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
cmake -S . -B build-cpu -DBUILD_TESTING=ON
cmake --build build-cpu --target arch_boundary_plan --parallel 1
ctest --test-dir build-cpu -R '^boundary_plan$' --output-on-failure
```

Replace `build-cpu` with the selected Windows build directory and keep its compiler environment. Provide `-DPython3_EXECUTABLE=<absolute-path>` when selecting a specific Python. Scientific Python checks need NumPy and h5py. CUDA-enabled test builds add NVCC work; `--target ARCH` builds the application without the standalone tests.

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
| Missing Windows DLL | Put the selected ABI's HDF5, compiler/OpenMP, CUDA and optional cuDSS runtime directories on PATH |
| GNU LTO fails in a Unicode build path | Move source/build/retained temporary directories to a short ASCII path |
| Electron fails to open on Linux | Check the graphical session/WSLg, `DISPLAY`, and missing entries in `ldd studio/node_modules/electron/dist/electron` |
| Windows Run cannot open a terminal | Install Windows Terminal and set its absolute `wt.exe` alias path in the tool configuration |
| Studio reports build freshness unknown | Full dependency coverage is unavailable for some toolchains, including MSVC/CUDA; keep the explicit build selection and inspect Build diagnostics |
| Preview exceeds a resource limit | Reduce preview sampling or initial AMR size; Windows uses a 1024 MiB committed-memory limit and user CPU time, while Linux uses address-space and process CPU limits |
| HDF5 data appears to be a short text file | Materialize the matching Git LFS table before using that EOS |

The UCRT64/GCC 16.2.0 Plotfile test entry has a compiler-specific IPO workaround in CMake; production targets retain IPO. EOS input parsing also follows the host library's representable range: MSVC rejects tested literals below binary64 range such as `1e-500`, while the Linux extractor accepts signed zero. Use the supplied canonical tables and representable values.
