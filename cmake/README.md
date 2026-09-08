# Build modules

[CMakeLists.txt](../CMakeLists.txt) owns project configuration, targets and shared
compiler controls. This directory contains focused helpers for CUDA route
registration, compiled device images and optional dependency discovery.

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

Note that all generated translation units inherently belong to the build directory. When adding a new route, always reuse the existing compile job pools and overarching optimization controls. Configuration instructions are provided in the [Reference](../docs/Reference.md); furthermore, measured build behavior is carefully recorded within the [core-build reference](../validation/backend/results/cold-core-first-law-20260907/release-909/README.md).
