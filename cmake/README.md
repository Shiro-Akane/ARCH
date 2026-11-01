# Build modules

[CMakeLists.txt](../CMakeLists.txt) declares the build order. These modules run in the project directory and share its source-property scope. User commands, platform dependencies and installation are documented in the [build guide](../docs/guides/Build.md).

## Target ownership

| Module | Responsibility |
| --- | --- |
| [BuildOptions.cmake](project/BuildOptions.cmake) | C++ standard, options, compiler cache, optimized-build policy and IPO linker selection |
| [Application.cmake](project/Application.cmake) | Application sources, numerical build contract, run identity, dispatch targets and Windows helpers |
| [BuildIdentity.cmake](project/BuildIdentity.cmake) | Source ownership and configured C/C++/CUDA/resource-compiler identity |
| [CustomNetworks.cmake](CustomNetworks.cmake) | Generated-package checks and shared network registry |
| [HostTests.cmake](tests/HostTests.cmake) | CPU and tooling tests; deferred I/O regression registration |
| [CudaBackend.cmake](cuda/CudaBackend.cmake) | CUDA/cuDSS discovery, source owners, compile pools and dependency ordering |
| [CudaTests.cmake](tests/CudaTests.cmake) | Device tests when CUDA and testing are enabled |
| [Studio.cmake](studio/Studio.cmake) | Optional Linux/Windows Electron assets, runtime, launcher and portable package |
| [Dependencies.cmake](dependencies/Dependencies.cmake) | OpenMP, HDF5, HighFive and KLU linkage |
| [Prepare.cmake](dependencies/Prepare.cmake) / [PrepareHdf5.cmake](dependencies/PrepareHdf5.cmake) | Explicit native tool selection and HDF5 C++/HL/zlib validation or ABI-specific preparation |
| [WindowsRuntime.cmake](project/WindowsRuntime.cmake) | Shared Core/Studio DLL directories, compiler environment and CLI `arch-env.cmd` |
| [WindowsTools.cmake](studio/WindowsTools.cmake) | Adds desktop-only tools to the shared Windows runtime data and emits Main's local tool manifest |

Host tests are declared before the optional CUDA backend. Dependency discovery then attaches libraries, and `arch_register_io_regression_tests()` registers tests that require HDF5 and KLU. Production presets leave test targets disabled unless requested.

The `<linux|windows>-<core|desktop>[-with-cuda]` profiles write to `build/<profile>/`. Their explicit preparation step publishes `prepared-toolchain.cmake` there; normal configuration and compilation remain CMake operations. Core preparation needs no Node or PE inspector. HDF5 preparation reuses a native C++/HL/zlib installation only after a link/run probe; HighFive and KLU retain their existing dependency owner.

The dependency module reduces repeated SuiteSparse status messages unless an explicit log level or verbose build is requested. It restores the log level before ARCH's summary. See [build output](../docs/guides/Build.md#build-output) for full logs.

## Compiler contract

Application and dispatch targets share optimization and floating-point settings. GNU/Clang Release builds use `-O3 -march=native`; MSVC uses its CMake Release settings. Supported LTO/IPO is enabled for optimized configurations. GNU/Clang receive `-fno-fast-math -fno-math-errno -ffp-contract=off`, MSVC receives `/fp:strict`, and NVCC receives the corresponding host settings and strict device flags. Keep the contract on both compilation and final linking. Build concurrency controls memory demand.

`arch_run_identity` compiles the UUID implementation before the directory-wide contract is attached. Linux uses `getrandom` and Windows uses system CNG entropy; the formatter and validator are shared. Windows application and Host-test executables share a private UTF-8 manifest.

The Plotfile test's `std::regex` entry translation unit uses `-fno-lto` on UCRT64/GCC 16.2.0 because that combination fails IPO. The exception is limited to that test source; production, writer/fingerprint objects and strict floating-point settings retain their normal contract.

## CUDA registration

- [CudaBurnNetworks.cmake](cuda/CudaBurnNetworks.cmake) supplies one built-in and generated-network compilation inventory.
- [CudaBurnDenseRoutes.cmake](cuda/CudaBurnDenseRoutes.cmake) and [CudaBurnSparseRoutes.cmake](cuda/CudaBurnSparseRoutes.cmake) register network/EOS owners and bind the shared burn implementations.
- [Templates](templates/README.md) emit thin C++ bindings into the build directory.
- [CudaCodeImages.cmake](cuda/CudaCodeImages.cmake) resolves the image set used by compilation and runtime compatibility checks.
- [FindCuDSS.cmake](dependencies/FindCuDSS.cmake) locates headers/libraries and defines the imported target. A runtime route also requires its registered implementation.

cuDSS discovery is optional. `ARCH_REQUIRE_CUDSS=ON` makes its absence a configuration error; with testing enabled, `arch-check-cudss` builds and runs the four provider checks and requires an available GPU. Ordinary CUDA and compact DenseLU routes do not depend on cuDSS. Run policy reports the selected backend and sparse provider through the shared Core checks.

Reuse compile pools and the numerical contract when adding a route. Keep each source's object owner explicit; host and device language choices remain distinct. Runtime policy selection belongs to the C++ factory. See [test registration](tests/README.md) for new checks.

## Studio and native Windows helpers

`ARCH_BUILD_STUDIO` defaults to `OFF`. Enable it in an existing Core tree to reuse its CPU/CUDA configuration. The optional targets are `arch-studio-assets`, `arch-studio-runtime`, `arch-studio` and `arch-studio-package`. `arch-studio` owns the Core and Windows helper dependencies, so callers build one target. Dependencies and assets have separate stamps: ordinary source edits reuse npm packages. The same packaging flow produces native Linux or Windows x64 portable directories; see the [Studio build instructions](../docs/guides/Build.md#studio-desktop).

On Windows, `arch_windows_process` supplies Job ownership and creation-time process identity. `arch_windows_build_publish` provides handle-based file publication, config publication and Plotfile read leases. Both use `src/core/files/WindowsDirectoryPins.h` and install beside ARCH. Run inherits only explicit lease/pin handles. Scientific parsers and serializers remain in their existing owners.

The process helper links `advapi32`, `shell32`, `ole32` and `uuid`; the publication helper links `ntdll` and `advapi32`. Test-only fault injection belongs to `arch_windows_build_publish_test` and is absent from the installed helper. The independent Run terminal is a runtime facility. Core CLI and Studio share the runtime directories and linked cuDSS DLL list from `WindowsRuntime.cmake`; Studio adds Node, GNU objdump and helper paths to its Main-private JSON. Generated source launchers select that file automatically, while explicit trusted environment overrides remain available. Machine-specific paths stay in the build tree and are excluded from generic portable packages.

## Link provenance

`ARCH_EMIT_LINK_DEPENDENCIES=ON` asks the selected linker to write `ARCH.link.d` in Makefile format. It defaults to `OFF`; unsupported linkers fail explicitly. GNU `windres` also emits preprocessing dependencies and binds the manifest resource to its object.

GNU provenance builds can additionally set `ARCH_RETAIN_LTO_LINK_INPUTS=ON`. This requires link-dependency emission and keeps the linker plugin's generated LTO inputs available for fingerprinting. It preserves IPO and floating-point flags, consumes local disk, and requires a persistent `TMPDIR`. The prepared GNU Core/desktop profiles and Host-managed GNU CPU profiles enable both options. A manual tree can select them explicitly when adding Studio. Native Windows GNU LTO requires an ASCII build/retention path; runtime paths may contain spaces and Unicode.

Compiler/linker support determines evidence coverage. See [platform support](../docs/releases/PlatformSupport.md) for freshness status and qualified toolchains, and the [build guide](../docs/guides/Build.md#optimization-compilers-and-target-gpus) for compiler selection.
