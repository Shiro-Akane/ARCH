# Native Windows CUDA

The corresponding native Windows CPU/CUDA/Studio functionality is implemented and validated for the tested profile within the existing scientific scope. Documented limits remain: full dependency freshness is unknown, the independent-console fallback is unqualified, and no new human dialog/normal-close acceptance is claimed. The [final correspondence record](../histroy/build/WindowsNativeCudaQualification-20261027.md) maps all 26 areas and their evidence; the CPU and Studio guides remain applicable.

Linux is recommended for primary scientific runs. Windows and Linux share the
solver, mathematical implementation, parameter format, and HDF5 format. The
Windows port is newer and may still have instability or bugs.

## Shared build requirements

This table describes CUDA. Windows CPU uses the separate [GCC/UCRT64 same-ABI HDF5/KLU recipe](WindowsNativeCpu.md); do not mix those libraries with MSVC.

| Component | Linux / WSL2 CUDA | Native Windows CUDA (MSVC) |
|---|---|---|
| NVCC host | An NVIDIA-supported GCC / Clang combination | An NVIDIA-supported MSVC toolset; do not reuse GCC/UCRT64 objects or libraries |
| Tools | CMake, Ninja, Linux CUDA Toolkit | Complete Kitware Windows CMake, Ninja, MSVC C++ tools, Windows SDK, Windows CUDA Toolkit |
| HDF5 / KLU | Libraries compatible with the host compiler | MSVC-built HDF5 C++ / HL and KLU; the same pinned HighFive headers |
| Optional sparse provider | cuDSS 0.8 matching the CUDA major version | The same 0.8 API, matching DLLs and import libraries |
| Inputs | Shared `.par` format and canonical EOS data | Adapt absolute paths; representable fixtures retain their bit checks. MSVC rejects three below-binary64 EOS tokens without publishing a value; see the reader note below |

The Linux reference uses NVCC 12.3/GCC 12. The accepted Windows profile uses NVCC 12.9.86, MSVC 19.44.35229 (14.44), SDK 10.0.26100, CMake 3.31.11, HDF5 1.14.6, SuiteSparse 7.13.0 and cuDSS 0.8 on an RTX 3060 Ti (SM86). These are tested configurations; revalidate other toolchain/hardware combinations.

MSVC 14.38 linked but failed the unchanged MC limiter NaN characterization. MSVC 14.44 preserves that `/O2 /fp:strict` contract and passed the native acceptance listed below, without `--allow-unsupported-compiler`. NVIDIA’s linked CUDA 12.9 table explicitly lists 193x; this additional measured profile is distinct from that published list. Approved A12–A18 retain formulas, thresholds, rejection checks and pool behavior. The commands below reproduce the measured 14.44 setup.

Follow NVIDIA's [Windows compiler support table](https://docs.nvidia.com/cuda/archive/12.9.1/cuda-installation-guide-microsoft-windows/index.html).
Do not use `--allow-unsupported-compiler`. Keep the reviewed
[cuDSS 0.8 API](https://docs.nvidia.com/cuda/cudss/0.8.0/getting_started.html);
cuDSS 1.0 is not an interchangeable replacement.

## Prepare the tools

1. Install the Visual Studio 2022 Build Tools C++ x64/x86 common tools, the
   tested MSVC 14.44 toolset, and Windows SDK. Installing the side-by-side compiler
   alone is insufficient: retain the common VC component providing
   `VC/Auxiliary/Build/vcvarsall.bat`, which NVCC needs for host discovery.
2. Use a complete [Kitware Windows distribution](https://cmake.org/download/).
   The existing MSYS2 CMake package lacked `cmcldeps.exe`, required for MSVC
   resource dependencies. Keep dependency tracking enabled.
3. Install the matching CUDA Toolkit. An alternative is to download NVCC,
   cudart, CCCL, and cuBLAS from NVIDIA's official redistributable manifest,
   verify each SHA256, and merge their `bin/include/lib/nvvm` directories into a
   separate prefix while retaining licenses. This campaign used that method
   without replacing the display driver.
4. Build HDF5 1.14.6 with C++ and HL enabled in a separate MSVC dependency
   prefix. Let the project build pinned SuiteSparse/KLU with the same compiler,
   or install that matching build. Do not select W0's GCC/UCRT64 dependency
   prefix.
5. OpenMP uses MSVC `/openmp:llvm /openmp:experimental` for the existing SIMD
   and atomic directives. Put the matching installed toolset's
   `Microsoft.VC143.OpenMP.LLVM` directory on the run PATH; the actual probe
   imports `libomp140.x86_64.dll`. Microsoft marks this runtime as
   nonredistributable: keep it external, provided by the user's toolchain; see
   [Microsoft's notes](https://learn.microsoft.com/en-us/cpp/build/reference/openmp-enable-openmp-2-0-support?view=msvc-170).
6. Sparse burn requires cuDSS 0.8's `include/cudss.h`, `lib/cudss.lib`, and
   `bin/cudss64_0.dll`. The runtime PATH must also resolve the matching cuBLAS
   and cuBLASLt DLLs.

Formal validation needs native Python 3.10+ plus NumPy and h5py. This campaign uses Python 3.14.10, NumPy 2.5.4 and h5py 3.16.0; Python 3.7 was a setup helper only. Provenance capture also needs a Git checkout and Git LFS with its filters enabled.

Keep this build and its tools separate from the qualified CPU build. Machine
paths and private tool configuration do not belong in the repository.

## Configure and build

Run this example in a **Developer Command Prompt / CMD**, using `^` for
continuations. Replace the example paths and enter the ARCH project root first.

```bat
call "C:\Tools\VS2022\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -vcvars_ver=14.44
set "ARCH_CMAKE=C:\Tools\cmake"
set "ARCH_CUDA=C:\Tools\cuda-12.9"
set "ARCH_MSVC_DEPS=C:\Tools\arch-msvc-deps"
set "ARCH_CUDSS=C:\Tools\cudss-0.8"
set "PATH=%ARCH_CUDA%\bin;%ARCH_MSVC_DEPS%\bin;%ARCH_CUDSS%\bin;C:\Tools\ninja;%PATH%"
"%ARCH_CMAKE%\bin\cmake.exe" -S . -B build-cuda-win -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe ^
  "-DCMAKE_CUDA_COMPILER=%ARCH_CUDA%/bin/nvcc.exe" ^
  "-DCMAKE_RC_COMPILER=C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe" ^
  "-DCUDAToolkit_ROOT=%ARCH_CUDA%" -DCMAKE_CUDA_ARCHITECTURES=native ^
  "-DCMAKE_PREFIX_PATH=%ARCH_MSVC_DEPS%" "-DHDF5_ROOT=%ARCH_MSVC_DEPS%" ^
  "-DCUDSS_ROOT=%ARCH_CUDSS%" -DARCH_ENABLE_CUDA=ON ^
  -DARCH_CUDA_HEAVY_COMPILE_JOBS=1 -DBUILD_TESTING=ON ^
  "-DPython3_EXECUTABLE=C:/Tools/python/python.exe" ^
  "-DARCH_RUNTIME_OUTPUT_DIRECTORY=%CD%/build-cuda-win/bin"
"%ARCH_CMAKE%\bin\cmake.exe" --build build-cuda-win --target ARCH --parallel 1
```

Windows CUDA selects shared `cudart`, matching the existing `CUDA::cudart`
link. Do not mix in `cudart_static`. The run PATH must include CUDA, cuDSS,
same-ABI HDF5 and the installed LLVM OpenMP runtime.

`native` targets the GPU visible during configuration. This campaign's actual
evidence uses `86-real`; an SM86 binary is not universal GPU coverage. Configure
appropriate code images when moving to another device. Start with one heavy
compile job and adjust only after observing available memory.

MSVC host code retains `/fp:strict`; NVCC retains `--fmad=false --ftz=false
--prec-div=true --prec-sqrt=true`. Both platforms preserve the strict FP
contract and disable fast-math. They also share the same UTF-8 manifest bytes:
MSVC embeds the manifest through its linker; GNU/windres uses the existing RC
source, avoiding duplicate manifest IDs.

## Backend selection and paths

Set `.par`'s `compute_backend` to `cpu`, `cuda`, or `auto`; `cuda_device` selects
the device ordinal. Selection happens at process startup: restart the process
to switch. A GPU backend requires `ARCH_ENABLE_CUDA=ON`; selecting `cpu` at
runtime does not turn that executable into a CPU-only build.

```ini
compute_backend = cuda
cuda_device = 0
```

From the project root, run `build-cuda-win\bin\ARCH.exe simulation/Sod/Sod.par`.
An explicit unsupported `cuda` request should fail clearly. Inspect the actual
backend selection record; automatic fallback or GPU detection alone is not
proof that a kernel executed.

Prefer project-relative paths and `/` inside `.par`, for example
`EOS_toolkit/tables/helmholtz/helm_table.dat`. Linux `/home/...` and WSL
`/mnt/c/...` are not native Windows paths; Windows `C:/...` is not a Linux path.
The input directory and process working directory may differ. Check path
resolution using the [build and runtime guide](Build.md), without rewriting
scientific data.

For models, configuration, and Plotfiles, see the [Studio guide](Studio.md).
Select an existing separate Ninja build with `--build-dir`. The private
`ARCH_STUDIO_WINDOWS_TOOLS` setup accepts optional `compilerEnvironment` with
only `INCLUDE`, `LIB`, and `LIBPATH`: explicit absolute local directory lists
from the initialized Microsoft Developer environment. HTTP/project/renderer
input cannot supply it. Main and Host share one validator and do not inherit
arbitrary `CL`, `_CL_`, or `LINK` options. Use the same absolute SDK `rc.exe`
path in this private context and `CMAKE_RC_COMPILER`; existing compiler/tool
bindings remain strict.

Preview stdin preserves CRLF bytes for the configuration revision. A `.par`
file can share its relative paths across systems when the working directory
is correct; changing line endings changes its raw-byte revision. For EOS
tables, MSVC formatted extraction rejects the tested below-binary64 literals
`1e-500`, `-1e-500`, and `+1e-500`; the buffered reader retains that failure
without publishing zero. The Linux extractor accepts signed zero for them.
The six representable reader fixtures and complete canonical-table field
comparison retain their exact bit checks.

Full MSVC/CUDA dependency freshness stays `unknown`; it does not claim the GNU
CPU `observed-build-inputs-v1` scope. Configure/Build, manifests, input hashes
and binary bindings retain their existing checks.

Actual managed MSVC/CUDA Build, initial Preview/AMR, GPU Run/Restart and formal Plotfile queries now have separate acceptance in the final record. Full dependency freshness remains unknown. Keep the matching supervisor/publisher from this CUDA build, the explicit compiler environment and a concise trusted PATH; do not copy an entire unrelated Developer PATH or a different build’s tool profile.

## Studio with the CUDA build

Prepare native Node, assets and Electron with the [desktop guide](WindowsStudioDesktop.md). Use the same CMake Studio targets with `build-cuda-win`, and build `arch_windows_process` and `arch_windows_build_publish` in that tree. Core remains usable without Studio.

Start an x64 Developer PowerShell for the selected MSVC toolset. Alternatively, enter `powershell -NoProfile` from the initialized Developer CMD above so that `INCLUDE`, `LIB` and `LIBPATH` are inherited. Adapt the desktop guide's `$tools` object for this build:

```powershell
$repo = (Resolve-Path .).Path
$build = Join-Path $repo 'build-cuda-win'
$vc = Split-Path (Get-Command cl.exe).Source
$sdk = 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64'
$omp = 'C:/Tools/VS2022/VC/Redist/MSVC/14.44.35112/debug_nonredist/x64/Microsoft.VC143.OpenMP.LLVM'
$tools.cmake = 'C:/Tools/cmake/bin/cmake.exe'
$tools.supervisor = Join-Path $build 'arch_windows_process.exe'
$tools.publisher = Join-Path $build 'arch_windows_build_publish.exe'
$tools.ninja = 'C:/Tools/ninja/ninja.exe'
$tools.rc = Join-Path $sdk 'rc.exe'
$tools.path = "$vc;$sdk;C:/Tools/arch-msvc-deps/bin;C:/Tools/cuda-12.9/bin;C:/Tools/cudss-0.8/bin;$omp;C:/msys64/ucrt64/bin;$env:SystemRoot/System32;$env:SystemRoot"
$tools.compilerEnvironment = @{
  INCLUDE = (($env:INCLUDE -split ';' | Where-Object { $_ }) -join ';')
  LIB = (($env:LIB -split ';' | Where-Object { $_ }) -join ';')
  LIBPATH = (($env:LIBPATH -split ';' | Where-Object { $_ }) -join ';')
}
$env:ARCH_STUDIO_WINDOWS_TOOLS = $tools | ConvertTo-Json -Depth 4 -Compress
& C:/Tools/node/node.exe studio/desktop/launch.mjs --project . --build-dir build-cuda-win --binary build-cuda-win/bin/ARCH.exe --source simulation/Sod/Sod.cpp --case Sod --config simulation/Sod/Sod_beginner.par
```

The inherited `$tools.objdump` and optional `$tools.terminal` come from the desktop setup. Replace every example path, including the installed OpenMP redist version; the private SDK `rc.exe` must exactly match `CMAKE_RC_COMPILER`. Keep MSVC HDF5/CUDA/cuDSS directories ahead of unrelated DLL prefixes.

Inside this CUDA-enabled executable, Config's `compute_backend` can select `cpu`, `cuda` or `auto` while retaining the same MSVC dependencies and build directory. Selecting a separate GCC CPU executable requires its matching DLL/tool context. Initial Preview remains an initialization operation; Run/Restart establishes the selected execution backend.

## Qualification status

Acceptance includes 142 distinct native scientific CTests (141 original-matrix tests plus the shared production t=0 topology oracle), 37 canonical backend cases with 9 resolution groups, 8 AMR restart routes and 9 comparisons, actual Host GPU Run/Restart/Plotfile readback, and zero errors in the complete original geometry memcheck. Linux passes 72 CPU CTests, 12 affected CUDA/real-fixture tests and 453 Studio tests; Windows passes 95 Studio tests and 7 link-evidence tests. Both rebuilt portable packages use actual Electron 44.9.1 and pass 225 production-file comparisons each; the current native window and bundled Host reach readiness. Existing W3 human dialog/display evidence stays separate. Release and Zenodo remain deferred.

The platform correspondence inventory covers build, CPU/OpenMP, backend selection, hydro, geometry/boundaries/AMR, EOS, diffusion, burning/generated networks/linear solves, gravity, HDF5/restart, custom cases and Studio workflows. A mapped entry is not platform qualification: production builds and the original functional matrix must pass before native Windows CUDA is called complete. A separate fixed-array regression verifies 448 raw-bit IEEE copies and element aliases on Linux Host and Windows GPU; it does not replace Tabular EOS failure-path checks.

To build and execute the registered scientific tests in the same Developer terminal:

```bat
"%ARCH_CMAKE%\bin\cmake.exe" --build build-cuda-win --parallel 1
"%ARCH_CMAKE%\bin\ctest.exe" --test-dir build-cuda-win -LE "^tooling$" --output-on-failure
```

The Linux-specific tooling suite is separate; native link-evidence tests cover its Windows parser. The canonical application matrix uses `tools/validate_backend_results.py` with `validation/backend/cases.json`; restart uses `tools/validate_cuda_amr_restart.py`. Both require the same build’s `ARCH.exe` and `arch_cuda_single_level_validation.exe`, and a new/empty output root. Keep complete resolution groups and original tolerances. Initial cold process loading exceeded an existing short metadata-query limit once during this campaign; the fail-closed result was retained. Final acceptance used the matching tool profile without extending Core/resource limits; universal cold-start stability is not claimed.
