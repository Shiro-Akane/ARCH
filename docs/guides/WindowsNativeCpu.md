# Native Windows 11 CPU CLI

For current complete Windows CPU/CUDA/Studio status and one-to-one correspondence, see the [2026-10-27 final record](../histroy/build/WindowsNativeCudaQualification-20261027.md). The W0/W1/W2/W3 observations below retain their dated scope. CUDA uses the separate [MSVC build and trusted tool context](WindowsNativeCuda.md).

[中文](WindowsNativeCpu.zh-CN.md) · [Build guide](Build.md) · [W0 toolchain preflight](WindowsNativeW0.md)

Studio process services and remaining frozen boundaries are documented in
[W2 platform services](WindowsStudioPlatform.md).

Windows and Linux use the same ARCH target, solvers, mathematical libraries and
HDF5 formats. This route uses GCC/MinGW-w64 **UCRT64**. It does not qualify MSVC,
clang-cl, CUDA or MPI. Native Studio has separate accepted [W3 evidence](WindowsStudioDesktop.md); daily operation is described in the shared [Studio guide](Studio.md). The ordinary CPU CLI needs no WSL, Node or
Python runtime; test tools have separate dependencies.

## Tools and directories

Prepare the tools and matching ABI dependencies using [W0](WindowsNativeW0.md).
The recorded combination is GCC `16.2.0-6`, CMake `4.4.4-3`, Ninja `1.13.2-1`,
HDF5 1.14.6, HighFive 2.9.0, minimal SuiteSparse/KLU 7.13.0 and UCRT64 zlib.
MSYS2 is rolling: record later versions and repeat applicable checks.

Use native PowerShell and adjust `$repo`. Prefer short ASCII source, tool and
build paths. Windows and Linux/WSL require separate build directories, caches,
objects and C++/LTO dependency sets.

```powershell
$tools = Join-Path $env:USERPROFILE 'Tools'
$ucrt = "$tools/msys64/ucrt64"
$deps = "$tools/arch-w0-deps"
$highfive = "$tools/arch-w0/sources/highfive-2.9.0"
$repo = 'C:/src/ARCH'
$build = "$repo/build-windows-cpu"
$install = "$tools/arch-cpu"
```

Runtime installation copies three EOS tables. Hydrate and verify the LFS
payloads for `EOS_toolkit/tables/helmholtz/helm_table.dat`,
`EOS_toolkit/tables/baryon/eos2.tab` and `eos4.tab` first:

```powershell
git -C $repo lfs pull --include="EOS_toolkit/tables/helmholtz/helm_table.dat,EOS_toolkit/tables/baryon/eos2.tab,EOS_toolkit/tables/baryon/eos4.tab" --exclude=""
if ($LASTEXITCODE -ne 0) { throw 'Required LFS assets are unavailable' }
```

Pointer text is not installed scientific data. This run used already verified
offline copies without downloading historical simulation data. Sod uses
IdealGas; Runtime installation still requires authentic table payloads.

Then select the environment for native compilation and execution:

```powershell
$env:Path = "$deps/bin;$ucrt/bin;$env:SystemRoot\System32;$env:SystemRoot"
$env:ComSpec = Join-Path $env:SystemRoot 'System32\cmd.exe'
$env:PATHEXT = '.COM;.EXE;.BAT;.CMD'
```

Exclude MSYS `usr/bin`, Anaconda HDF5 DLLs and other libstdc++ instances.
Keep native backslashes in `ComSpec` and `.EXE` lookup in `PATHEXT`.

## Configure, build and install

These commands use the existing CMake graph, with cached pinned HighFive source
and installed KLU. They do not add another solver or dependency implementation.

```powershell
& "$ucrt/bin/cmake.exe" -S $repo -B $build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_C_COMPILER:FILEPATH=$ucrt/bin/gcc.exe" `
  "-DCMAKE_CXX_COMPILER:FILEPATH=$ucrt/bin/g++.exe" `
  "-DCMAKE_RC_COMPILER:FILEPATH=$ucrt/bin/windres.exe" `
  "-DCMAKE_PREFIX_PATH:PATH=$deps" "-DHDF5_ROOT:PATH=$deps" `
  "-DFETCHCONTENT_SOURCE_DIR_HIGHFIVE:PATH=$highfive" `
  -DARCH_ENABLE_CUDA=OFF -DARCH_ENABLE_OPENMP=ON `
  -DARCH_ENABLE_KLU=ON -DARCH_FETCH_SUITESPARSE=OFF `
  -DARCH_BUILD_STUDIO=OFF -DBUILD_TESTING=OFF `
  "-DARCH_RUNTIME_OUTPUT_DIRECTORY:PATH=$build/bin" `
  "-DCMAKE_INSTALL_PREFIX:PATH=$install"
if ($LASTEXITCODE -ne 0) { throw 'ARCH configuration failed' }

& "$ucrt/bin/cmake.exe" --build $build --target ARCH --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'ARCH build failed' }
& "$ucrt/bin/cmake.exe" --install $build --component Runtime
if ($LASTEXITCODE -ne 0) { throw 'ARCH installation failed' }
```

The application is `$install/bin/ARCH.exe`; samples/tables are under
`$install/share/arch`. This is not a standalone distribution: the selected
UCRT64 and HDF5 DLLs must remain on PATH. It does not package Studio or create a
release. Existing CMake owners retain strict FP, OpenMP and production IPO;
formulas and tolerances are unchanged.

## Run and select paths

```powershell
Set-Location $repo
& "$install/bin/ARCH.exe" Sod "$install/share/arch/simulation/Sod/Sod_beginner.par"
if ($LASTEXITCODE -ne 0) { throw 'ARCH simulation failed' }
```

Relative output paths use the launch working directory. CLI arguments and
`.par` values such as `out_dir`/`restart_file` may use `C:/work/output` or
`C:\work\output`; do not globally rewrite business strings. The application
and Host C++ test tools share a process-local UTF-8 manifest. Save parameter
files as UTF-8; no system-locale change is needed. This run checks Chinese
characters and spaces in local NTFS installation, parameter and output paths.
It does not establish long-path, UNC, network-filesystem or cross-host mapping
support. WSL `/mnt/c/...` and `/home/...` paths do not become Windows paths
merely by changing separators.

Restart still uses `restart = true` and `restart_file = ...`, with the existing
physical-compatibility rules. An 8-step, 64-cell Sod engineering sample resumes
from the actual step-1 checkpoint to step 8. The existing comparator passes
zero-tolerance comparisons for this sample's restart and OpenMP 1/4 results.
This adds no universal bitwise gate and does not qualify Sod convergence, all
physics models or cross-platform restart.

## Focused checks and compiler limitation

`BUILD_TESTING=ON` also needs a Python interpreter. Full project Python tools
use a supported Python 3.10+ environment. This run's older Windows Python is
used only for stdlib orchestration/parent-process checks, not scientific-Python
qualification. After building the selected test targets, run:

```powershell
$python = 'C:/path/to/python.exe'
& "$ucrt/bin/cmake.exe" -S $repo -B $build -DBUILD_TESTING=ON `
  "-DPython3_EXECUTABLE:FILEPATH=$python"
if ($LASTEXITCODE -ne 0) { throw 'Test configuration failed' }
& "$ucrt/bin/cmake.exe" --build $build --parallel 2 --target ARCH `
  arch_platform_files arch_cuda_single_level_validation arch_compensated_sum `
  arch_tabular_strict arch_helm_components arch_reduction_contract `
  arch_plotfile_publication arch_checkpoint_compatibility sparse_klu_regression
if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
& "$ucrt/bin/ctest.exe" --test-dir $build --output-on-failure --no-tests=error `
  -R '^(platform_files|running_image_identity|checkpoint_temporal_comparison|compensated_sum|tabular_strict_math|helm_components|reduction_contract|plotfile_publication|checkpoint_compatibility|sparse_klu_161_equations)$'
if ($LASTEXITCODE -ne 0) { throw 'Focused native checks failed' }
```

`arch_cuda_single_level_validation` is the existing Host checkpoint comparator;
it does not enable a GPU backend. Unbuilt, skipped and inapplicable cases are not passes. The new image test
renames a real running executable before its first digest, replaces its old
launch path with different bytes, and checks the original image's SHA for
ASCII and Unicode paths.

On this UCRT64/GCC 16.2.0 installation, the Plotfile test harness's valid
`std::regex` fails under IPO before any Grid operation. Isolated and mixed
object controls locate the failure to this harness. For this platform/compiler
version only, CMake appends `-fno-lto` to that test source. Production ARCH,
HDF5Writer, FileFingerprint and the remaining checks retain IPO. Test source,
assertions, `-O3` and strict FP remain unchanged. Recheck on compiler changes;
the local reproducer is not an upstream-confirmed diagnosis.

Core shares UUID formatting/validation and changes only its OS entropy source.
Windows fingerprints the main image's mapped backing file through a read-only
handle, excludes write/delete sharing while reading, and rechecks its mapped
name. Retrieval/open/read failures throw, with no `argv[0]` or empty fallback.
Actual replacement tests support ordinary local NTFS; availability during
concurrent renaming, arbitrary filesystem providers and hostile kernels are
outside this evidence.

Mathematical libraries, scientific schemas, restart gates and error budgets retain their contracts. The CLI sample does not replace full-model science qualification. Native Host/Preview and window acceptance are recorded separately in [W2](WindowsStudioPlatform.md) and [W3](WindowsStudioDesktop.md), with daily operation in the [shared guide](Studio.md). GPUs/MPI require separate qualification. Zenodo and release decisions remain separate.
