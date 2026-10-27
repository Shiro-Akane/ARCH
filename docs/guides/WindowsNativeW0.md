# Native Windows 11 toolchain preflight (W0)

[中文](WindowsNativeW0.zh-CN.md) · [Build guide](Build.md)

For subsequent application builds/runs, see [Native Windows CPU CLI](WindowsNativeCpu.md).
The W0 observations below retain their original probe profiles and scope.

This page prepares/checks the native CPU toolchain, ABI and selected mathematical contracts. Application build/install/run/restart are covered by [W1](WindowsNativeCpu.md), and complete native Studio by [W3](WindowsStudioDesktop.md); daily operation uses the shared [Studio guide](Studio.md). The W0 observations below retain their original scope. Mathematical libraries, scientific schemas and tolerances remain unchanged; CUDA/MPI require separate qualification.

## Recorded combination

The Windows 11 x64 W0 environment on 2026-10-24 uses this combination. MSYS2 is a
rolling distribution: later installs may provide other versions. Record actual
versions and repeat applicable checks rather than treating this table as a
permanent lock.

| Component | Version/selection |
| --- | --- |
| GCC C/C++, libgcc, libstdc++, libgomp | MSYS2 UCRT64 `16.2.0-6` |
| CMake/Ninja | `4.4.4-3` / `1.13.2-1` |
| zlib | UCRT64 `1.3.2-3`, used for HDF5 compression |
| HDF5 | Upstream `hdf5_1.14.6`, built with the same GCC; shared C/CXX/HL, zlib enabled |
| HighFive | The existing project version `v2.9.0`; no header upgrade |
| SuiteSparse | Existing fallback version `v7.13.0`; KLU, AMD, COLAMD and required BTF/config only |
| Parallel runtime | GCC libgomp; minimal SuiteSparse adds no OpenMP/BLAS runtime |

Current repository HDF5/SuiteSparse packages may differ from these versions.
Use a separate dependency prefix. Do not mix Anaconda/MSVC HDF5 C++ DLLs or
objects from another C++/LTO ABI into this GNU graph. Installed KLU discovery is
not globally version-pinned; 7.13.0 here matches the existing fallback recipe,
without adding a new rejection rule.

## Install the tools

Download and verify the bootstrap checksum/signature using the
[official MSYS2 instructions](https://www.msys2.org/docs/installer/). This run uses
`msys2-base-x86_64-20260927.sfx.exe` under `%USERPROFILE%\Tools\msys64`, without
changing the system PATH, default compiler or existing Anaconda. Use a short,
ASCII-only local NTFS installation path without spaces.

The self-extractor is a GUI executable. Wait for its actual exit in PowerShell:

```powershell
$tools = Join-Path $env:USERPROFILE 'Tools'
$p = Start-Process .\msys2-base-x86_64-20260927.sfx.exe `
  -ArgumentList '-y', "-o$tools" -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "MSYS2 extraction failed: $($p.ExitCode)" }
& "$tools\msys64\ucrt64.exe"
```

In the UCRT64 shell, perform a full update and install native packages:

```bash
pacman -Syu
# If the update closes the terminal, reopen UCRT64 and continue the full update.
pacman -Syu --needed mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-libgomp mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-zlib
pacman -Q
```

Package management may use the MSYS shell. Compile and execute the following
checks from native PowerShell. Preparing Windows tools does not require changes
to the existing Linux/WSL environment.

## Select this process's prefixes

The examples use this run's layout. `$sources` holds pinned upstream source;
`$deps` receives dependencies built by the selected native toolchain. Use fresh
build and dependency directories when changing compiler/ABI.

```powershell
$tools = Join-Path $env:USERPROFILE 'Tools'
$ucrt = "$tools\msys64\ucrt64"
$work = "$tools\arch-w0"
$sources = "$work\sources"
$deps = "$tools\arch-w0-deps"
$env:Path = "$deps\bin;$ucrt\bin;$env:SystemRoot\System32;$env:SystemRoot"
$env:ComSpec = Join-Path $env:SystemRoot 'System32\cmd.exe'
$env:PATHEXT = '.COM;.EXE;.BAT;.CMD'
& "$ucrt\bin\g++.exe" -v
& "$ucrt\bin\cmake.exe" --version
& "$ucrt\bin\ninja.exe" --version
$common = @('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
  "-DCMAKE_C_COMPILER=$ucrt\bin\gcc.exe",
  "-DCMAKE_CXX_COMPILER=$ucrt\bin\g++.exe",
  "-DCMAKE_MAKE_PROGRAM=$ucrt\bin\ninja.exe",
  "-DCMAKE_INSTALL_PREFIX=$deps", '-DCMAKE_CXX_STANDARD=20',
  '-DCMAKE_POLICY_VERSION_MINIMUM=3.5')
```

Keep `ComSpec` in native backslash spelling. Setting it to
`C:/Windows/.../cmd.exe` reproduced a CMake command-chain syntax error before GCC
ran. Ordinary filesystem paths accepting both separators does not establish
that every command interpreter accepts the same self-launch string. Declare
custom CMake path cache arguments as `:PATH`/`:FILEPATH` and normalize them;
untyped `C:\Users...` strings can become invalid escapes in generated scripts.
Do not replace separators globally in business/protocol strings.
When launching Windows tools from WSL, preserve native `PATHEXT` too. Without
`.EXE` lookup, windres cannot invoke extensionless `gcc` for preprocessing even
though a direct `gcc.exe` command succeeds.

## Build pinned dependencies

Prepare these source directories from the official tags and retain their
archive/commit identities:

- [HDF5 `hdf5_1.14.6`](https://github.com/HDFGroup/hdf5/releases/tag/hdf5_1.14.6) → `$sources\hdf5-1.14.6`.
- [HighFive `v2.9.0`](https://github.com/BlueBrain/HighFive/releases/tag/v2.9.0) → `$sources\highfive-2.9.0`.
- [SuiteSparse `v7.13.0`](https://github.com/DrTimothyAldenDavis/SuiteSparse/releases/tag/v7.13.0) → `$sources\suitesparse-7.13.0`.

For a minimal SuiteSparse extraction retain top-level CMake plus
`SuiteSparse_config / cmake_modules / KLU / AMD / COLAMD / BTF`. Do not patch
upstream source or ARCH's dependency declarations. Check every exit code, stop
on failure and retain its log; cached outputs/old DLLs are not a new build.

```powershell
& "$ucrt\bin\cmake.exe" -S "$sources\hdf5-1.14.6" -B "$work\build-hdf5" @common `
  -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF -DBUILD_TESTING=OFF `
  -DHDF5_BUILD_CPP_LIB=ON -DHDF5_BUILD_HL_LIB=ON -DHDF5_BUILD_FORTRAN=OFF `
  -DHDF5_BUILD_TOOLS=OFF -DHDF5_BUILD_EXAMPLES=OFF -DHDF5_ENABLE_PARALLEL=OFF `
  -DHDF5_ENABLE_Z_LIB_SUPPORT=ON -DHDF5_ENABLE_SZIP_SUPPORT=OFF `
  -DHDF5_ENABLE_NONSTANDARD_FEATURE_FLOAT16=OFF "-DZLIB_ROOT:PATH=$ucrt"
& "$ucrt\bin\cmake.exe" --build "$work\build-hdf5" --parallel 2
& "$ucrt\bin\cmake.exe" --install "$work\build-hdf5"

& "$ucrt\bin\cmake.exe" -S "$sources\suitesparse-7.13.0" -B "$work\build-suitesparse" @common `
  '-DSUITESPARSE_ENABLE_PROJECTS=klu;amd;colamd' -DKLU_USE_CHOLMOD=OFF `
  -DSUITESPARSE_USE_CUDA=OFF -DSUITESPARSE_USE_FORTRAN=OFF -DSUITESPARSE_REQUIRE_BLAS=OFF `
  -DBLA_VENDOR=Generic -DSUITESPARSE_USE_OPENMP=OFF -DSUITESPARSE_USE_PYTHON=OFF `
  -DSUITESPARSE_DEMOS=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON
& "$ucrt\bin\cmake.exe" --build "$work\build-suitesparse" --parallel 2
& "$ucrt\bin\cmake.exe" --install "$work\build-suitesparse"

& "$ucrt\bin\cmake.exe" -S "$sources\highfive-2.9.0" -B "$work\build-highfive" @common `
  "-DHDF5_ROOT:PATH=$deps" -DHIGHFIVE_UNIT_TESTS=OFF -DHIGHFIVE_EXAMPLES=OFF `
  -DHIGHFIVE_BUILD_DOCS=OFF -DHIGHFIVE_USE_BOOST=OFF -DHIGHFIVE_USE_EIGEN=OFF `
  -DHIGHFIVE_USE_XTENSOR=OFF -DHIGHFIVE_PARALLEL_HDF5=OFF
& "$ucrt\bin\cmake.exe" --install "$work\build-highfive"
```

The optional HDF5 `_Float16` extension fails here because `FLT16_MAX` is absent.
Disable that unused extension with its upstream option, retaining ARCH's
binary64/native-long-double semantics and schema. `BLA_VENDOR=Generic` matches
ARCH's minimal KLU recipe. HighFive 2.9 declares CMake 3.1 minimum; CMake 4 can
configure that external dependency with `CMAKE_POLICY_VERSION_MINIMUM=3.5`
without source patches. None of these checks replaces subsequent native
qualification of the top-level ARCH build/FetchContent behavior.

## Focused checks and scope

Actually execute Windows PE programs and record the C/C++ versions, target
triplet, CRT and actual DLL paths/hashes. The `x86_64-w64-mingw32` triplet alone
does not distinguish UCRT from MSVCRT. Package provenance plus compile/link/run
evidence determines the ABI; a filename suffix or directory name is insufficient.

W0 retains strict FP (no fast-math or implicit FMA contraction), the existing
C/C++ IPO archive-link probe and these observations:

- Effective `long double` digits/`max_digits10`, a runtime-fed increment beyond
  binary64 precision and exact native text roundtrip; subnormals, signed zero,
  finite checks and explicit FMA residuals.
- Existing CompensatedSum, HelmComponentsRegression, strict Tabular and
  SparseKLURegression tests; the matching real Helm table rather than its LFS pointer.
- Existing serial/OpenMP 1- and 4-thread reduction tests. A separate diagnostic
  observes the actual worker team and loaded libgomp path; this is not module
  sampling of each original reduction-test process.
- HDF5 CXX/HL and HighFive 2.9 create/close/reopen/scalar/vector/attribute
  readback and target-filesystem path behavior. Small samples do not qualify
  production Plotfiles/checkpoints.

For example, compile the unchanged compensated-sum test from a frozen NTFS
source copy, putting outputs in the work directory:

```powershell
$repo = 'C:\path\to\ARCH'
$flags = @('-std=c++20', '-O3', '-march=native', '-DNDEBUG',
  '-fno-fast-math', '-fno-math-errno', '-ffp-contract=off')
& "$ucrt\bin\g++.exe" @flags "-I$repo\src" "-I$repo\tests" `
  "$repo\tests\host\numerics\test_compensated_sum.cpp" -o "$work\compensated-sum.exe"
& "$work\compensated-sum.exe"
```

Do not change frozen math/tests or Studio I/O to fit a compiler. Existing bitwise
requirements stay within their original scopes; do not add a universal ULP gate
or relax tolerances. Record LLP64 `long` width versus Linux LP64 and inspect
affected consumers. Long paths, UNC, file-identity races and full Studio
startup need their own evidence rather than inference from an ordinary readback.

## Results and the UTF-8 path condition

The candidate passes native C/C++ IPO checks. `long double` has 64 significand
bits and `max_digits10=21`; runtime extended precision and text roundtrip pass.
The unchanged compensated-sum, Helm, strict Tabular, KLU 161-equation and
serial/OpenMP 1-/4-thread reduction tests pass; the actual OpenMP team is 4.
KLU's maximum absolute error is `2.22045e-16` against the unchanged `1e-11`
test limit. Small-sample HDF5 CXX/HL/HighFive 2.9 compressed and bitwise readback
passes. These are focused W0 results, not a full native ARCH/Studio qualification.

The first profile passes 10 of 11 cases: with process code page 936, a UTF-8
narrow string fails to open the intended Unicode path. Three additional probes
with a process-local UTF-8 manifest report `GetACP()=65001` and pass ordinary
file I/O, the unchanged Helm loader and HDF5/HighFive I/O on paths containing
Chinese characters and spaces. Retain the initial failure; the two profiles
must not be reported as an all-green default configuration.

Use the [Windows process-local UTF-8 setting](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page)
without changing the system locale. The probes use this `utf8.manifest`:

```xml
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly manifestVersion="1.0" xmlns="urn:schemas-microsoft-com:asm.v1">
  <assemblyIdentity type="win32" name="ARCH.W0.NativeUtf8Probe" version="1.0.0.0"/>
  <application>
    <windowsSettings>
      <activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>
    </windowsSettings>
  </application>
</assembly>
```

Put `1 24 "utf8.manifest"` in the adjacent `utf8.rc`, compile it with the same
UCRT64 `windres.exe` and link the resource into the probe. In CMake, enable `RC`
and add the `.rc` to the relevant target sources. This run applies that profile
only to isolated probes, without production CMake or frozen-library changes.
W1 should establish UTF-8 at application boundaries and verify cross-process
arguments. A manifest does not repair mixed-encoding strings or qualify UNC,
long paths or every Win32 API.
