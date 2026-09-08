# Build modules

[CMakeLists.txt](../CMakeLists.txt) shows the build order. These modules describe
the targets and their shared settings. They are included in the project
directory, so splitting the files does not create new build directories or
change the scope of source-file properties.

## Build flow

| Module | Responsibility |
| --- | --- |
| [BuildOptions.cmake](BuildOptions.cmake) | Language standard, user options, compiler cache and optimized-build policy |
| [Application.cmake](Application.cmake) | Application sources, shared numerical contract, diffusion library and dispatch target |
| [CustomNetworks.cmake](CustomNetworks.cmake) | Generated-package checks and the shared network registry; included by the application module |
| [tests/HostTests.cmake](tests/HostTests.cmake) | Host contract tests and the deferred I/O regression registration |
| [CudaBackend.cmake](CudaBackend.cmake) | CUDA/cuDSS discovery, explicit source owners, compile pools and phase barriers |
| [tests/CudaTests.cmake](tests/CudaTests.cmake) | CUDA test executables and CTest cases, only when both CUDA and testing are enabled |
| [Dependencies.cmake](Dependencies.cmake) | OpenMP, HDF5, HighFive and KLU linkage for declared targets |

Host contract tests are declared before the optional CUDA backend. The
dependency module then attaches the required libraries, and the top-level file
calls `arch_register_io_regression_tests()` once HDF5 and KLU are known. Keeping
this order also keeps production-only builds free of test targets.

## CUDA helpers

- [CudaBurnNetworks.cmake](CudaBurnNetworks.cmake): one compilation inventory
  for built-in and device-capable generated networks. Runtime policy selection
  remains in the C++ factory.
- [CudaBurnDenseRoutes.cmake](CudaBurnDenseRoutes.cmake): emit compact Ideal/Helm
  network/EOS wrappers and attach their objects to the backend.
- [CudaBurnSparseRoutes.cmake](CudaBurnSparseRoutes.cmake): register sparse
  network/EOS owners and link the actual cuDSS implementation when available.
  The factory also retains the explicit unsupported-route error path.
- [templates/](templates/README.md) and
  [CudaCustomDenseRoute.cu.in](CudaCustomDenseRoute.cu.in): thin generated C++
  bindings to the shared burn implementation; they contain no separate ODE or
  reaction mathematics.
- [CudaCodeImages.cmake](CudaCodeImages.cmake): resolve the configured CUDA image
  list used by compilation and runtime compatibility checks.
- [FindCuDSS.cmake](FindCuDSS.cmake): locate headers and libraries and define the
  imported target. Discovery alone does not enable a production route.
- [SelectIpoLinker.cmake](SelectIpoLinker.cmake): probe a mixed C/C++ static-library
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
