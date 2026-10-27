# Build modules

[CMakeLists.txt](../CMakeLists.txt) shows the build order. These modules describe
the targets and their shared settings. They are included in the project
directory, so splitting the files does not create new build directories or
change the scope of source-file properties.

## Build flow

| Module | Responsibility |
| --- | --- |
| [BuildOptions.cmake](project/BuildOptions.cmake) | Language standard, user options, compiler cache and optimized-build policy |
| [Application.cmake](project/Application.cmake) | Application sources, shared numerical contract, compiled run identity, diffusion and dispatch targets; private Windows UTF-8 resources |
| [BuildIdentity.cmake](project/BuildIdentity.cmake) | Actual source owners and configured C/C++/CUDA/RC compiler profile, with native absolute-path handling |
| [CustomNetworks.cmake](CustomNetworks.cmake) | Generated-package checks and the shared network registry; included by the application module |
| [tests/HostTests.cmake](tests/HostTests.cmake) | Host contract tests and the deferred I/O regression registration |
| [CudaBackend.cmake](cuda/CudaBackend.cmake) | CUDA/cuDSS discovery, explicit source owners, compile pools and phase barriers |
| [tests/CudaTests.cmake](tests/CudaTests.cmake) | CUDA test executables and CTest cases, only when both CUDA and testing are enabled |
| [studio/Studio.cmake](studio/Studio.cmake) | Optional native Linux/Windows x64 Electron assets, runtime, CLI entry and portable directory package |
| [Dependencies.cmake](dependencies/Dependencies.cmake) | OpenMP, HDF5, HighFive and KLU linkage for declared targets |

Host contract tests are declared before the optional CUDA backend. The
dependency module then attaches the required libraries, and the top-level file
calls `arch_register_io_regression_tests()` once HDF5 and KLU are known. Keeping
this order also keeps production-only builds free of test targets.

The dependency module temporarily reduces SuiteSparse's repeated status output
only when no explicit log level or verbose build was requested. It restores
the log level before ARCH's own summary, leaves notices and diagnostics visible,
and keeps FetchContent in the caller's scope. The
[build-output guide](../docs/guides/Build.md#build-output) describes how to request
full logs without changing compiler or linker settings.

Release application and dispatch targets share `-O3` and supported LTO/IPO.
The build contract disables fast-math and implicit contraction, and removes
unused host libm errno side effects. Keep this contract on both compilation
and final linking; limit concurrent jobs to control build memory.

`arch_run_identity` compiles the existing Core UUID implementation once, before
the directory-wide contract is attached, so OS headers do not leak into HighFive
or numerical consumers. Linux uses `getrandom`; Windows uses system CNG entropy.
The same UUID formatter and validator serve both. Windows application and Host
test executables use the same private UTF-8 manifest; no second solver or I/O
format is introduced. See the [native CPU guide](../docs/guides/WindowsNativeCpu.md).

UCRT64/GCC 16.2.0 has a locally reproduced IPO failure in the Plotfile test's
`std::regex` entry translation unit. Only that test source gets `-fno-lto` on
this exact platform/compiler combination. Its unchanged assertions and strict
FP remain active; production and writer/fingerprint IPO are retained.

On Windows, `arch_windows_process` supplies Job ownership and creation-time
identity to the existing Studio runners. Its entry point is excluded from the
ARCH source glob on every platform. Runtime installs the helper beside ARCH;
it does not link the solver or translate Preview resource budgets. See the
[W2 platform guide](../docs/guides/WindowsStudioPlatform.md) for the opt-in Host
tool setup, native checks and protected I/O blockers.

## CUDA helpers

- [CudaBurnNetworks.cmake](cuda/CudaBurnNetworks.cmake): one compilation inventory
  for built-in and device-capable generated networks. Runtime policy selection
  remains in the C++ factory.
- [CudaBurnDenseRoutes.cmake](cuda/CudaBurnDenseRoutes.cmake): emit compact Ideal/Helm
  network/EOS wrappers and attach their objects to the backend.
- [CudaBurnSparseRoutes.cmake](cuda/CudaBurnSparseRoutes.cmake): register sparse
  network/EOS owners and link the actual cuDSS implementation when available.
  The factory also retains the explicit unsupported-route error path.
- [templates/](templates/README.md) and
  [CudaCustomDenseRoute.cu.in](templates/CudaCustomDenseRoute.cu.in): thin generated C++
  bindings to the shared burn implementation; they contain no separate ODE or
  reaction mathematics.
- [CudaCodeImages.cmake](cuda/CudaCodeImages.cmake): resolve the configured CUDA image
  list used by compilation and runtime compatibility checks.
- [FindCuDSS.cmake](dependencies/FindCuDSS.cmake): locate headers and libraries and define the
  imported target. Discovery alone does not enable a production route.
- [SelectIpoLinker.cmake](project/SelectIpoLinker.cmake): probe a mixed C/C++ static-library
  link and select a compatible linker while retaining supported LTO/IPO.

Generated translation units belong in the build directory. When adding a route,
reuse the existing compile pools and shared numerical contract. Keep each
source's canonical object owner explicit; use loops for repeated settings or
ordered target lists, not to hide differences between host and device code.

The [build guide](../docs/guides/Build.md) explains user configuration and
parallelism. The [test-module guide](tests/README.md) explains where to register
new checks. Measured compilation costs remain in the
[core-build reference](../validation/backend/results/cold-core-first-law-20260907/release-909/README.md);
shortening CMake source alone does not reduce template-instantiation cost.

Local provenance tooling may opt into ARCH_EMIT_LINK_DEPENDENCIES=ON. ARCH then asks the selected linker to write Makefile-style ARCH.link.d in the build directory. This is disabled by default and adds no numerical flags. A linker without --dependency-file support fails explicitly; absence of the file must not be treated as complete dependency coverage.

GNU C++ provenance builds may additionally opt into
ARCH_RETAIN_LTO_LINK_INPUTS=ON with ARCH_EMIT_LINK_DEPENDENCIES=ON.
The GNU linker plugin's diagnostic retention option keeps its generated LTO
inputs available for fingerprinting. It is OFF by default, does not disable IPO
or change floating-point flags, and fails configuration for other compiler IDs
or without link dependency emission. Retained objects consume local disk;
use a Host-owned persistent TMPDIR before adopting it in a managed profile.
The independent GCC 13/mold microprobe produced identical control/retention ELF
bytes, but that is not an ARCH equivalence or complete build-coverage claim.
The Host-owned local CPU profile enables this option together with linker input
evidence; other existing trees are not silently migrated. No existing build cache or
Manifest is upgraded merely because this option exists.

## Linux Studio components

Use `cmake --preset studio-cpu-release -DARCH_STUDIO_NODE=/absolute/path/to/node` and `cmake --build build-studio-cpu --parallel 2` to build the scientific CPU executable and `build-studio-cpu/bin/arch-studio` together. This preset matches the Host-owned local build directory; arbitrary existing caches are not adopted silently. `ARCH_BUILD_STUDIO` defaults to OFF in ordinary scientific builds.

The optional targets are `arch-studio-assets`, `arch-studio-runtime`, `arch-studio` (Core plus desktop entry), and `arch-studio-package` (portable Linux x64 package). The first build prepares locked npm dependencies and explicitly installs the locked Electron runtime even if npm lifecycle hooks are disabled. Packaging includes Host Node and h5wasm; no CUDA build is required. System graphical libraries, WSLg under WSL, and `/usr/bin/xterm` plus `flock` for Run/Restart are prerequisites. The source entry depends on the checkout; the package is a separate output under `studio/.local/desktop-release`. See the [Studio guide](../docs/guides/Studio.md).

## Native Windows Build evidence

The shared application graph builds `arch_windows_process` and
`arch_windows_build_publish` on Windows, and installs both with Runtime. The latter
is an opaque, handle-based file primitive under `core/files`; it
does not link the solver or write scientific formats. With `BUILD_TESTING=ON`,
`arch_windows_build_publish_test` adds test-only fault seams and registers
`windows_build_manifest_publication` and `windows_config_and_read_operations`;
these seams are absent from the installed
publisher.

The historical publisher target also provides bounded config publication and
Plotfile read leases to the original Studio owners. Both helpers share
`src/core/files/WindowsDirectoryPins.h`; Run inherits only explicit lease/pin
handles. Process/lock support links Windows `advapi32`, `shell32`, `ole32` and
`uuid`; publication links `ntdll` and `advapi32`. No extra third-party dependency,
solver variant or alternate scientific serializer is introduced. The common
Host requires fixed native tool paths; optional Windows Terminal is runtime
bootstrap configuration, not a CMake prerequisite. Approved Windows Preview
limits live in the existing Core WorkerLimits owner: committed memory and Job
user CPU, distinct from Linux RLIMIT_AS/user+system CPU. The platform guide
documents policy admission and the standalone native resource harness; no new
CMake option or runtime dependency is needed.

For GNU windres, opt-in linker evidence also enables actual preprocessing depfiles
and binds the manifest resource as an object dependency. This fixes empty Ninja
RC records without relaxing the shared dependency parser. RC numerical flags and
application mathematics are unchanged. Native GNU LTO currently requires an ASCII
build/retention path; space-containing paths have execution evidence. See the
[Windows platform guide](../docs/guides/WindowsStudioPlatform.md) for fixed Host
tools, PE evidence budgets, reproduction commands and remaining frozen capabilities.
