# Platform support

[中文](PlatformSupport.zh-CN.md) · [Build](../guides/Build.md) · [Features](../Features.md) · [Studio](../guides/Studio.md)

ARCH supports Linux/WSL2 and native Windows 11 x64. Linux is recommended for primary scientific work. Windows provides the same CPU/CUDA CLI and Studio workflows within the supported scientific scope, but may still have platform-specific instability or bugs.

## Toolchains

| Component | Linux / WSL2 | Windows 11 x64 |
| --- | --- | --- |
| CPU | GCC, OpenMP, Ninja and CMake | GCC/UCRT64 with matching HDF5/KLU libraries |
| CUDA | NVCC with a compatible host compiler | NVCC/MSVC, Windows SDK and matching MSVC-ABI libraries |
| Studio | Native Node 24+, Electron and a graphical session; WSLg under WSL | Native x64 Node 24+ and Electron |
| Run terminal | `xterm` and `flock` | Windows Terminal and the native process helper |
| Build directories | Native Linux cache and dependencies | Separate GCC/UCRT64 and MSVC/NVCC caches and dependencies |

The Windows CUDA reference uses NVCC 12.9.86, MSVC 19.44.35229 (toolset 14.44), SDK 10.0.26100, CMake 3.31.11, HDF5 1.14.6, SuiteSparse 7.13.0 and optional cuDSS 0.8 on an RTX 3060 Ti (SM86). MSVC 14.38 fails the IEEE NaN characterization used by ARCH; use the tested 14.44 toolset for this recipe. NVIDIA's CUDA 12.9 compiler table lists 193x, so the tested 14.44 combination is an additional measured configuration. The Linux CUDA reference uses NVCC 12.3/GCC 12. Installation commands and dependency setup are in [Build](../guides/Build.md).

## Shared functionality

Both platforms provide CPU/OpenMP execution, `cpu`/`cuda`/`auto` selection, the same fluid methods, geometry, AMR, EOS, diffusion, reaction-network and gravity implementations, HDF5 output and CPU/CUDA restart within each platform. The [feature list](../Features.md) specifies supported model and method combinations.

Studio provides model discovery, parameter editing, Configure/Build, initial-field and initial-AMR Preview, Run/Restart, and read-only Plotfile metadata, raw samples, native-cell queries and display overviews. Native desktop packages share the renderer, Host and packaging flow. Initial Preview uses CPU Setup/Init even when the selected executable supports CUDA.

## Platform limits

- Full MSVC/CUDA dependency and dynamic-loader freshness remains `unknown`; Build can produce an executable while dependency coverage remains unknown. Studio records the available input evidence separately.
- Windows Run/Restart uses configured Windows Terminal. The independent-console fallback is not a supported launch route.
- Windows Preview limits use committed memory and Job user CPU time; Linux uses address-space and user-plus-system CPU limits. The numerical requests and their budgets retain their platform-specific policy.
- Native GNU LTO requires an ASCII build/retention path. Runtime paths with UTF-8 and spaces are tested; UNC/network filesystems and arbitrary long paths are outside the tested filesystem scope.
- A cold metadata request can exceed its short timeout on some installations. Check the selected toolchain/DLL context and retry after the program has loaded.
- Desktop packages are portable directories. Core, toolchain/DLL dependencies and the terminal are installed separately; signed installers and auto-update are not provided.

MPI, AMD/HIP and Apple Metal are not supported backends. General radiation transport is outside the implemented physics. Jeans/JENS full GPU evolution, general-EOS applicability and coupled-model limits follow the [Reference](../Reference.md) and [Validation](../../validation/README.md).

## v1.4.0 validation references

The v1.4.0 platform comparison includes 142 native scientific CTests, 37 canonical backend cases in nine resolution groups, eight CPU/CUDA AMR restart routes and nine comparisons, and actual Studio GPU Run/Restart/Plotfile readback. Linux coverage includes 72 CPU CTests, 12 affected CUDA/fixture checks and 453 Studio tests; Windows includes 95 Studio tests and seven link-evidence checks. The Linux and Windows portable packages use Electron 44.9.1.

Inputs, source identities, individual results and failure records are available in the [v1.4.0 platform record](https://github.com/Shiro-Akane/ARCH/blob/v1.4.0/docs/histroy/build/WindowsNativeCudaQualification-20261027.json). [Scientific validation](../../validation/README.md) provides the reference problems and error budgets for each physical module.
