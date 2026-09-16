# Test registration

[HostTests.cmake](HostTests.cmake) declares CPU-side contracts and tooling
checks. Its `arch_register_io_regression_tests()` function is called after
dependency discovery for checkpoint, tabular-EOS and KLU regressions.
[CudaTests.cmake](CudaTests.cmake) adds device checks and application witnesses
when both `ARCH_ENABLE_CUDA` and `BUILD_TESTING` are enabled.

Register related cases together and reuse settings only when their compiler,
linkage and test behavior really match. Some `.cu` fixtures deliberately compile
as ordinary C++ and call the production backend; others instantiate shared
mathematics with NVCC. Preserve those language choices, device-unavailable skip
codes, required test ordering and compile-pool limits.

Neither file belongs to a separate CMake subdirectory. Both run in the project
directory so source paths, output locations and the numerical build contract
stay consistent. User build presets disable tests unless requested; building
the `ARCH` target does not compile the standalone test executables.

For running or adding tests, start with [tests/README.md](../../tests/README.md).
