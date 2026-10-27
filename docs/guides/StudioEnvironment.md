# Studio requirements: Linux / WSL and Windows

[Workflow](Studio.md) · [Detailed setup and troubleshooting](StudioEnvironment.zh-CN.md)

**Linux is recommended**, including WSL2 with WSLg. Native Windows11 x64 supports the complete CPU/CUDA Studio workflow, with newer support that may still have instability or bugs. Both routes use the same Host/scientific services. Core alone needs no Node, npm or Electron; desktop support does not require CUDA compilation. See [native CUDA](WindowsNativeCuda.md) for its separate compiler dependencies and trusted tool context.

| Requirement | Linux / WSL2 | Native Windows11 x64 |
| --- | --- | --- |
| Core and build tools | Native C++20, CMake, Ninja, HDF5/OpenMP | GCC/UCRT64 plus matching-ABI dependencies and native helpers; [setup](WindowsNativeW0.md) / [build](WindowsNativeCpu.md) |
| CUDA Core option | NVCC with compatible host libraries | Separate MSVC/NVCC dependencies and private compiler context; [CUDA setup](WindowsNativeCuda.md) |
| Source desktop runtime | Native Linux Node24+, npm, Electron | Native Windows x64 Node24+, npm, Electron |
| Window | Graphical Linux session / WSLg and libraries below | Native Windows desktop |
| Confirmed Run/Restart | `xterm`, `flock` | Configured Windows Terminal and native supervisor/publisher |

Keep each OS's Node, Core, CMake cache and dependencies together. Portable packages include their own native Host Node and h5wasm; Core and OS/tool prerequisites remain external. The [shared user guide](Studio.md#paths-on-linux-and-windows) explains paths and `.par` portability.

## Linux / WSL2

Run the GUI as a normal Linux user.

Required components are C++20, CMake 3.22+, Ninja and HDF5 development libraries for the CPU Core; Linux Node 24+ and npm for source builds; the locked Electron runtime; Linux GTK3/NSS/GBM/ALSA/X11 libraries; a working graphical session; and `/usr/bin/xterm` plus `flock` for independent Run/Restart terminals. Python 3.10+ is for validation; scientific tests additionally need h5py and NumPy. The portable package includes Host Node and h5wasm but still needs Core and system libraries.

Ubuntu 24.04 package names:

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends \
  build-essential cmake ninja-build libhdf5-dev curl xz-utils \
  libgtk-3-0t64 libnss3 libgbm1 libasound2t64 \
  libx11-xcb1 libxkbcommon0 xterm util-linux
```

Install a supported Linux Node runtime using your existing Node manager or the official Linux archive, preserving its license and checking the version-matched checksum. This workstation uses Node 24.21.0 at `$HOME/.local/opt/node-studio`. Other installations may use a different absolute path.

From the ARCH root:

```bash
export PATH="$HOME/.local/opt/node-studio/bin:$PATH"
cmake --preset studio-cpu-release -DARCH_STUDIO_NODE="$HOME/.local/opt/node-studio/bin/node"
cmake --build build-studio-cpu --parallel 2
build-studio-cpu/bin/arch-studio --project "$PWD" \
  --binary build-studio-cpu/bin/ARCH --case Sod --config simulation/Sod/Sod.par
```

The preset matches the Host-owned local CPU build directory. Other existing caches are not adopted silently. The generated entry depends on the checkout. `arch-studio-package` creates the separate portable Linux package; its Host uses bundled Node unless explicitly overridden. An invalid explicit override fails.

Check `DISPLAY`, `xterm`, `flock`, and `ldd studio/node_modules/electron/dist/electron` when launch fails. Resolve missing libraries before starting the desktop.

## Native Windows11 x64

Follow the [native desktop guide](WindowsStudioDesktop.md) after the [CPU setup](WindowsNativeCpu.md). It uses the same `arch-studio-assets`, `arch-studio-runtime` and `arch-studio-package` targets. CMake invokes native Node's npm-cli.js, without requiring npm.ps1. The checkout entry is `arch-studio.cmd`; the portable entry is `arch-studio-bin.exe`.

Set `ARCH_STUDIO_WINDOWS_TOOLS` to the trusted tool/DLL context for the chosen build. Editing and Preview need no terminal; qualified Run/Restart sets `terminal` to `wt.exe`. Use that build’s native helpers: GCC/UCRT64 HDF5 for the CPU route, or matching MSVC HDF5/CUDA/cuDSS with the compiler environment in the [CUDA guide](WindowsNativeCuda.md). Keep unrelated Anaconda/MSYS/other-ABI DLLs off the Core PATH. Adapt Linux absolute `.par` paths; `/home` and `/mnt/c` are not Windows drive paths.

## Developer verification

For this workstation's scientific tests, activate conda `work` and configure `Python3_EXECUTABLE` from that environment. Other users may choose any Python environment with the required packages. The following Linux process-interface requirements do not describe the native Windows supervisor.

The complete Tooling suite and process resource guard also require Linux Python's `os.pidfd_open` and `signal.pidfd_send_signal`. This workstation's conda `work` Python 3.12.14 does not expose them; `/usr/bin/python3` does. Use `work` for scientific CTest and system Python for the guard and complete Tooling. A version number alone does not establish these capabilities.

For developer validation on Ubuntu 24.04:

```bash
sudo apt-get install --no-install-recommends \
  python3 python3-numpy python3-h5py mold shellcheck
/usr/bin/python3 -c 'import os, signal, h5py, numpy; assert hasattr(os, "pidfd_open") and hasattr(signal, "pidfd_send_signal")'
/usr/bin/python3 -m unittest discover -s tests/tooling -p 'test_*.py' -v
```

`mold` and `shellcheck` are validation dependencies, not desktop user requirements. Formal CI additionally requires a nonempty suite with no skipped controls. Missing process interfaces or dependencies must be resolved before acceptance. A system-Python resource guard may still launch the scientific command using the conda environment.
