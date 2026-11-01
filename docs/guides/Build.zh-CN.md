# 构建 ARCH

[English](Build.md) · [快速开始](../../README.zh-CN.md#构建) · [Studio](Studio.zh-CN.md)

ARCH 在 Linux、WSL2 和原生 Windows 11 x64 上使用同一个 CMake 项目。**科学计算推荐使用 Linux。** Windows 在[功能清单](../Features.zh-CN.md)所列范围内提供相同的 CPU、CUDA 和 Studio 功能，但较新的平台支持可能仍有不稳定性和缺陷。

<a id="from-source-to-cuda-and-studio"></a>
## 从源码构建 CUDA 与 Studio

安装[对应平台的工具](#tools-and-dependencies)后，从仓库根目录执行四条命令。

Linux／WSL2：

```bash
cmake -DARCH_PROFILE=linux-desktop-with-cuda -P cmake/dependencies/Prepare.cmake
cmake --preset linux-desktop-with-cuda
cmake --build --preset linux-desktop-with-cuda
build/linux-desktop-with-cuda/bin/arch-studio
```

Windows，在可用原生 Node 24+ 和 GNU objdump 的 **MSVC 14.44 x64 Developer Command Prompt** 中执行：

```bat
cmake -DARCH_PROFILE=windows-desktop-with-cuda -P cmake/dependencies/Prepare.cmake
cmake --preset windows-desktop-with-cuda
cmake --build --preset windows-desktop-with-cuda
build\windows-desktop-with-cuda\bin\arch-studio.cmd
```

Studio 打开空白的项目选择页。选择项目、构建目录与执行物，再选择已注册模型和 `.par`；界面用法见[图解指南](Studio.zh-CN.md)。两种构建都具备 CPU 和 CUDA 执行能力，通过参数文件的 `compute_backend = cpu`、`cuda` 或 `auto` 在运行时选择后端。

| 步骤 | 作用 | 何时重复 |
| --- | --- | --- |
| Prepare：准备 | 选择原生工具，验证现有 HDF5 C++/HL 或构建同 ABI 的 HDF5，记录本机配置 | 首次设置，或更换工具／依赖位置时 |
| Configure：配置 | 读取 profile，通过已有依赖入口提供 HighFive/KLU，生成编译规则 | 修改 CMake 选项后 |
| Build：编译 | 编译 Core；desktop profile 同时编译 Studio 和所需原生辅助程序 | 修改源码后 |
| Launch：启动 | 打开 Studio；也可直接运行 `bin/ARCH` 或 `bin/ARCH.exe` | 每次使用时 |

首次准备和配置会按需下载依赖，之后的构建会复用它们。各操作系统和编译器 ABI 使用独立的构建目录；源码目录内的 Studio 依赖也分别属于所在的操作系统。

<a id="cpu-build"></a>
<a id="cuda-build"></a>
<a id="native-windows-cpu-and-cuda"></a>
<a id="choose-a-profile"></a>
## 选择构建 profile

准备、配置和编译三步使用同一个 profile 名。构建目录统一为 `build/<profile>/`，执行物位于其 `bin/` 下。

| 内容 | Linux／WSL2 | Windows 11 x64 |
| --- | --- | --- |
| CPU Core | `linux-core` | `windows-core` |
| CPU + CUDA Core | `linux-core-with-cuda` | `windows-core-with-cuda` |
| CPU Core + Studio | `linux-desktop` | `windows-desktop` |
| CPU + CUDA Core + Studio | `linux-desktop-with-cuda` | `windows-desktop-with-cuda` |

纯 Core 构建不需要 Node 或 objdump。在同一构建目录启用 Studio 后，需要安装桌面依赖。Desktop profile 默认包含窗口及相关工具；`with-cuda` 为同时保留 CPU 能力的执行物加入 CUDA。[README](../../README.zh-CN.md#构建)给出纯 Core 的命令；已有 Core 构建目录可按[桌面部分](#build-and-launch-the-desktop)直接加入 Studio。

<a id="presets-and-build-options"></a>
八种 profile 均使用 Release 和 Ninja，默认启用 OpenMP、KLU，关闭测试，并使用一个编译任务。CUDA profile 针对配置时可见的 GPU 生成代码。GNU profile 保留构建输入证据，便于之后在 Studio 中使用。`cmake --list-presets` 列出全部 preset；[高级配置](#advanced-configuration)说明手工准备环境所用的直接配置 preset。

<a id="tools-and-dependencies"></a>
## 工具与依赖

| 组件 | Linux／WSL2 | 原生 Windows 11 x64 |
| --- | --- | --- |
| CPU 编译器 | C++20 GCC；手工构建也可选择 Clang | MSYS2 **UCRT64** GCC 及其 libgomp 运行库 |
| CUDA 编译器 | NVCC 与兼容 GCC Host | NVCC、MSVC 14.44 x64、Visual Studio C++ common tools 和 Windows SDK |
| 构建工具 | Git、Ninja；CPU 需 CMake 3.22+，CUDA 需 3.25.2+ | 原生 Git/Ninja；GCC 使用 UCRT64 CMake，MSVC 使用完整 Kitware Windows CMake |
| 必需库 | 含 zlib 的 HDF5 C++/HL，以及 OpenMP | 使用所选编译器 ABI 构建的同类库 |
| 共用头文件／CPU 稀疏求解器 | HighFive 2.9.0 与 KLU；源码后备使用 SuiteSparse 7.13.0 | 同样版本，GCC 和 MSVC 各自构建 |
| CUDA 运行环境 | CUDA Toolkit 12.0+ 和兼容 NVIDIA 驱动 | Windows CUDA Toolkit 和兼容 NVIDIA 驱动 |
| 稀疏 CUDA 燃烧 | 可选 cuDSS **0.8**，与 CUDA 主版本匹配 | cuDSS 0.8 导入库／DLL，以及匹配的 cuBLAS/cuBLASLt DLL |
| 可选桌面 | 原生 x64 Node 24+、npm、图形会话和 Electron 系统库 | 原生 x64 Node 24+、npm 和 **GNU objdump** |
| 测试 | Python 3.10+；科学检查需要 NumPy、h5py | 原生 Python 3.10+；科学检查需要 NumPy、h5py |

准备步骤通过编译并运行 C++/HL/zlib 小程序验证 HDF5。没有可用安装时，会在与编译器对应的目录中构建 HDF5 1.14.6 和 zlib 1.3.1。HighFive 与 KLU 继续由共用的 [Dependencies.cmake](../../cmake/dependencies/Dependencies.cmake) 管理。`ARCH_ENABLE_OPENMP=OFF` 关闭 CPU 多线程；`ARCH_ENABLE_KLU=OFF` 移除 CPU 稀疏求解器。

已测试组合包括 Linux CUDA 12.3/GCC 12；Windows CPU GCC 16.2.0/CMake 4.4.4；以及 Windows CUDA 12.9.86、MSVC 19.44.35229（toolset 14.44）、SDK 10.0.26100 和 CMake 3.31.11。Windows 依赖使用 HDF5 1.14.6 与 SuiteSparse 7.13.0。MSYS2 是滚动发行版，当前软件包版本可能不同。

<a id="linux--wsl2-installation"></a>
### Linux／WSL2 安装

Ubuntu 24.04：

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends build-essential cmake ninja-build git git-lfs libhdf5-dev
```

CUDA 构建另需 NVIDIA Linux CUDA Toolkit 和兼容 Host 编译器。准备脚本优先选择已安装的 GCC 12，也可用[自定义路径](#custom-tools-and-offline-preparation)选择其他受支持安装。WSL2 的 GPU 驱动安装在 Windows 侧，Linux Toolkit 按 [NVIDIA WSL 指南](https://docs.nvidia.com/cuda/wsl-user-guide/index.html)安装。

<a id="windows-cpu-installation"></a>
### Windows CPU 安装

将 [MSYS2](https://www.msys2.org/docs/installer/) 安装到 `C:/msys64` 等较短的本地路径，在其 **UCRT64** 终端运行：

```bash
pacman -Syu
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libgomp mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

若更新要求关闭终端，重新打开 UCRT64 后继续。安装原生 Git，再用 **PowerShell** 执行 ARCH 命令。准备、配置和编译前，将 UCRT64 工具加入该终端的 PATH：

```powershell
$env:Path = 'C:/msys64/ucrt64/bin;' + $env:Path
```

准备脚本提供匹配的 HDF5，CMake 提供 HighFive/KLU。MSYS2 安装在其他位置时，向准备命令传入 `-DARCH_UCRT_ROOT=C:/your/msys64/ucrt64`，并将该位置的 `bin` 加入 PATH。此编译器环境中不要混入 MSYS `usr/bin`、Anaconda 库或其他 C++ 运行库。

<a id="windows-cuda-installation"></a>
### Windows CUDA 安装

安装 Visual Studio 2022 Build Tools 的 C++ x64/x86 工具、**MSVC 14.44** 和 Windows SDK。保留含 `VC/Auxiliary/Build/vcvarsall.bat` 的 VC common tools，NVCC 会使用它。另需完整的 [Kitware Windows CMake](https://cmake.org/download/)、原生 Ninja 和 Windows CUDA Toolkit；MSVC 资源依赖跟踪需要 CMake 自带的 `cmcldeps.exe`。

打开为 14.44 初始化的 x64 **Developer Command Prompt**。自定义安装位置例如：

```bat
call "C:\Tools\VS2022\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -vcvars_ver=14.44
```

确保该终端可用 CMake、Ninja、Git 和 NVCC，再执行 Windows CUDA profile。准备脚本构建或选择 MSVC ABI 的 HDF5，并记录匹配的编译器、SDK 和 OpenMP 运行库目录。GCC/UCRT64 库使用独立 ABI 和安装目录。

MSVC 14.38 无法通过已有的 NaN 契约检查。NVIDIA 的 [CUDA 12.9 编译器表](https://docs.nvidia.com/cuda/archive/12.9.1/cuda-installation-guide-microsoft-windows/index.html)列出 193x 系列；上述 14.44 是 ARCH 独立测试的组合。选择其他 Toolkit／toolset 时需确认兼容性，不使用 `--allow-unsupported-compiler` 绕过检查。

ARCH 在 MSVC 上使用 `/openmp:llvm /openmp:experimental`。准备脚本从所选 toolset 的 `Microsoft.VC143.OpenMP.LLVM` 目录定位 `libomp140.x86_64.dll`。该 [Microsoft 运行库](https://learn.microsoft.com/en-us/cpp/build/reference/openmp-enable-openmp-2-0-support?view=msvc-170)不可再分发，保留在本机工具链安装中。

<a id="run-and-select-paths"></a>
## 运行与路径

Linux／WSL2，以 CPU profile 为例：

```bash
export OMP_NUM_THREADS=4
build/linux-core/bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

Windows Command Prompt；CUDA 构建保留已初始化的 Developer 环境：

```bat
call build\windows-core\bin\arch-env.cmd
set OMP_NUM_THREADS=4
build\windows-core\bin\ARCH.exe Sod simulation/Sod/Sod_beginner.par
```

使用 CUDA 时，将两处路径中的 profile 换成 `linux-core-with-cuda` 或 `windows-core-with-cuda`，并在输入副本中修改已有的 `compute_backend` 赋值。`cuda_device` 选择 GPU 序号。Core 的 `--check-run` 与 Studio 的 Run 准备显示实际后端和选择原因，Run 日志及 `*_backend_plan.txt` 记录执行信息。

Windows 配置会根据所选依赖和编译器目录生成 `bin/arch-env.cmd`。调用它即可设置 CLI 运行环境；链接 cuDSS 时，还会检查所需 DLL。Studio 自动使用同一份运行环境数据。

**`.par` 内相对路径以计算工作目录为基准，不以参数文件所在目录为基准。** Studio 使用项目根目录，CLI 使用终端当前目录。推荐 `out_dir = output/first_sod` 等相对值。`/mnt/c/src/ARCH` 是 WSL 路径，Windows 原生路径是 `C:/src/ARCH`，仅替换斜杠不能完成转换。

`.par` 使用 UTF-8，接受 LF 和 CRLF。Windows 执行物带有进程级 UTF-8 manifest。GNU LTO 的源码／构建目录使用较短 ASCII 路径；普通本地输入输出路径支持空格和 Unicode。UNC／网络文件系统和任意长路径不在已测试配置内。Studio 的项目相对路径统一使用 `/`，详见 [Studio 路径说明](Studio.zh-CN.md#paths-on-linux-and-windows)。

<a id="studio-desktop"></a>
<a id="linux-studio-desktop"></a>
## Studio 桌面

安装原生 x64 Node **24+** 及 npm。Linux／WSL2 还需要图形会话（WSL 下为 WSLg）和 Electron 系统库。Ubuntu 24.04：

```bash
sudo apt-get install --no-install-recommends libgtk-3-0t64 libnss3 libgbm1 libasound2t64 libx11-xcb1 libxkbcommon0 xterm util-linux
```

Run/Restart 打开独立终端：Linux 使用 `/usr/bin/xterm` 和 `/usr/bin/flock`，Windows 使用原生终端。参数编辑和 Preview 可在没有独立 Run 终端时使用。

Windows Studio 通过 GNU `objdump.exe` 检查构建输入。GCC/UCRT64 已包含它；MSVC/CUDA 桌面也可仅安装 MSYS2 的 `mingw-w64-ucrt-x86_64-binutils`，无需再构建一份 GCC Core。已验证的检查格式为 GNU objdump；LLVM objdump 和 MSVC dumpbin 使用不同接口。需要自定义位置时，用 `ARCH_STUDIO_OBJDUMP` 指定 GNU objdump 的绝对路径。

<a id="build-and-launch-the-desktop"></a>
### 构建并启动桌面

给已有 Core profile 加入 Studio 时，在**同一个构建目录**中启用选项。

Linux／WSL2：

```bash
cmake -S . -B build/linux-core -DARCH_BUILD_STUDIO=ON
cmake --build build/linux-core --target arch-studio
build/linux-core/bin/arch-studio
```

Windows PowerShell，使用 Core 的编译器环境：

```powershell
cmake -S . -B build/windows-core -DARCH_BUILD_STUDIO=ON
cmake --build build/windows-core --target arch-studio
& .\build\windows-core\bin\arch-studio.cmd
```

CUDA 构建使用已有的 `*-core-with-cuda` 目录；Windows CUDA 从初始化后的 Developer 终端启动 PowerShell。`arch-studio` 会构建 Core、界面资源、锁定版本的 Electron 及两个所需 Windows 辅助程序。普通源码修改后复用 npm 依赖；删除 `node_modules` 后会重新安装。Windows 经 Node 调用 npm，不受 PowerShell 的 `npm.ps1` 策略影响。

Core profile 已保留 GNU 构建输入证据。给任意手工 GNU 构建目录加入 Studio 时，还需[两项证据选项](#advanced-configuration)。无参数启动打开空白页，也可用[模型／参数 CLI 选项](Studio.zh-CN.md#指定模型与参数启动)直接打开工作台。

<a id="windows-tool-configuration"></a>
### Windows 工具配置

生成的源码启动器自动选择 `build/<profile>/studio/windows-tools.json`。这个本机文件记录 CMake/Ninja/资源编译器、GNU objdump、原生辅助程序、DLL 路径及 MSVC 的 `INCLUDE`／`LIB`／`LIBPATH`。打开项目时检查已链接 cuDSS 所需的 DLL；空白启动页尚不打开项目，也不检查 Core 运行库。

使用自定义环境或便携包时，将 `ARCH_STUDIO_WINDOWS_TOOLS_FILE` 设置为该可信本机 JSON 文件的路径。显式设置的 `ARCH_STUDIO_WINDOWS_TOOLS` JSON 内容优先。这些设置属于启动环境，不写入 `.par` 或工作台字段；含本机绝对路径的配置不放入通用便携包。

<a id="package-the-desktop"></a>
### 打包桌面

在目标操作系统中，使用已配置的桌面构建目录生成便携目录：

```text
cmake --build build/linux-desktop-with-cuda --target arch-studio-package
```

Windows 使用对应编译器环境和 `build/windows-desktop-with-cuda`。产物位于 `studio/.local/desktop-release/`：

| 系统 | 目录 | 启动入口 |
| --- | --- | --- |
| Linux | `arch-studio-bin-linux-x64` | `arch-studio` |
| Windows | `arch-studio-bin-win32-x64` | `arch-studio-bin.exe` |

保留完整目录，其中包含 Electron、独立 Host Node、h5wasm 和许可证。Core、编译器／运行库依赖和独立 Run 终端另行安装。Windows 选择上一节的本机工具配置；构建目录 `bin/` 中的源码启动器则依赖其源码 checkout。

打包脚本读取 `package.json` 中的 Electron 精确版本，并核对 lockfile 和已安装包。`electron_config_cache` 可指向已缓存且校验和匹配的官方归档。保留 Node、Electron/Chromium 和 h5wasm/HDF5 许可证。当前生成便携目录，尚未配置签名、安装器或自动更新。

<a id="custom-tools-and-offline-preparation"></a>
## 自定义工具与离线准备

工具或依赖位置在准备时传入，放在 `-P` 之前，然后正常配置。例如：

```bash
cmake -DARCH_PROFILE=linux-core-with-cuda \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-12 -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 \
  -DHDF5_ROOT=/opt/arch-deps -P cmake/dependencies/Prepare.cmake
cmake --preset linux-core-with-cuda
```

| 准备选项 | 用途 |
| --- | --- |
| `CMAKE_C_COMPILER`、`CMAKE_CXX_COMPILER`、`CMAKE_CUDA_COMPILER` | 选择匹配的原生工具链；Linux NVCC 使用所选 C++ Host |
| `CMAKE_MAKE_PROGRAM`、`CMAKE_RC_COMPILER` | 选择 Ninja 与 Windows 资源编译器 |
| `ARCH_UCRT_ROOT` | UCRT64 安装位置，默认 `C:/msys64/ucrt64` |
| `ARCH_STUDIO_NODE`、`ARCH_STUDIO_OBJDUMP` | 桌面运行时与 Windows GNU PE 检查工具 |
| `HDF5_ROOT`、`HDF5_DIR`、`KLU_DIR`、`CMAKE_PREFIX_PATH` | 复用兼容的已安装依赖 |
| `CUDAToolkit_ROOT`、`CUDSS_ROOT` | 选择 CUDA 与可选 cuDSS 安装 |
| `ARCH_DEPENDENCY_ROOT` | 下载、源码和 ABI 构建缓存，默认 `build/dependencies` |
| `ARCH_PREPARE_JOBS` | HDF5 依赖构建并发数，默认 2 |
| `ARCH_HDF5_SOURCE_DIR`、`ARCH_ZLIB_ARCHIVE` | 已有 HDF5 1.14.6 源码与锁定的 zlib 1.3.1 归档 |
| `FETCHCONTENT_SOURCE_DIR_HIGHFIVE`、`FETCHCONTENT_SOURCE_DIR_SUITESPARSE` | 供正常 CMake 依赖入口使用的本地源码 |
| `ARCH_PREPARE_OFFLINE=ON` | 使用已有依赖安装、源码和归档 |

显式选择的 HDF5 目录必须通过原生 C++/HL/zlib 检查。准备结果写入 `build/<profile>/prepared-toolchain.cmake`，更换位置时重新生成；更换编译器／ABI 时使用新的 profile 构建目录。离线准备需要本地 HighFive，以及已安装 KLU 或本地 SuiteSparse。EOS 表按[独立说明](#offline-tables-and-lfs-bandwidth)准备。

<a id="cudss-and-sparse-burning"></a>
## cuDSS 与稀疏燃烧

cuDSS **0.8** 是可选依赖，`ARCH_ENABLE_CUDSS=ON` 时进行查找。用 `CUDSS_ROOT` 选择包含 `include/cudss.h` 和匹配库的安装；Windows 还需导入库、`bin/cudss64_0.dll` 及 CUDA 的 cuBLAS/cuBLASLt DLL。配置会将这些 Windows DLL 记录到共用运行环境中。

| 请求的路径 | cuDSS 不可用时 |
| --- | --- |
| 普通 CUDA 流体计算和小型 DenseLU 燃烧 | 仍可使用 |
| 稀疏燃烧显式要求 CUDA 执行或 `cudss` 求解器 | 在 Run 准备时拒绝 |
| 稀疏燃烧采用 `compute_backend = auto` 和自动求解器选择 | 对应编译路径可用时，可以选择 CPU KLU |

Core 和 Studio 共用 Run 检查，显示实际后端和选择原因。CPU 稀疏燃烧需要保留 KLU，并编译相应 network/EOS 路径，见[生成网络](../../validation/network/README.md)。Linux 仅在所选路径需要时加载厂商库，无关 Preview 不预加载这些库。

若发布包承诺 cuDSS 支持，可要求该依赖并按原数值阈值执行四项 GPU 检查：

```bash
cmake --preset linux-core-with-cuda -DARCH_REQUIRE_CUDSS=ON -DBUILD_TESTING=ON
cmake --build build/linux-core-with-cuda --target arch-check-cudss
```

Windows 换成对应 profile 和构建目录。`ARCH_REQUIRE_CUDSS=ON` 会让缺少该依赖成为配置错误；`arch-check-cudss` 构建并执行检查，GPU 不可用时判为验证失败。

<a id="advanced-configuration"></a>
## 高级配置

准备 profile 使用同一个 CMake 项目。已有手工管理环境时，可以直接配置，例如：

```bash
cmake -S . -B build/custom -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DARCH_ENABLE_CUDA=OFF -DCMAKE_PREFIX_PATH=/opt/arch-deps \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build/custom/bin"
cmake --build build/custom --target ARCH --parallel 1
```

首次配置时选择匹配的 C/C++ 编译器。Windows CPU 的 `CMAKE_C_COMPILER`、`CMAKE_CXX_COMPILER`、`CMAKE_RC_COMPILER` 分别选择 UCRT64 的 `gcc.exe`、`g++.exe`、`windres.exe`。Windows CUDA 使用已初始化 MSVC 14.44 终端中的 `cl.exe`、SDK `rc.exe` 和 NVCC，启用 `ARCH_ENABLE_CUDA=ON` 并提供 MSVC ABI 的依赖。`ARCH_RUNTIME_OUTPUT_DIRECTORY` 指定执行物位置；直接 CMake 配置时默认写入源码树的 `bin/`。

给手工 GNU 构建目录加入 Studio 和构建输入证据：

```text
cmake -S . -B build/custom -DARCH_BUILD_STUDIO=ON -DARCH_EMIT_LINK_DEPENDENCIES=ON -DARCH_RETAIN_LTO_LINK_INPUTS=ON
cmake --build build/custom --target arch-studio
```

这些选项保留 linker/LTO 输入供新鲜度检查使用，IPO 和浮点选项不变。MSVC 构建使用已有证据路径，不启用 GNU LTO 选项。用户显式选择的选项保留在缓存中。

手工准备环境仍可使用直接 preset：`cpu-release`、`cuda-release`、`studio-cpu-release`、`cuda-debug`，目录分别为 `build-cpu`、`build-cuda`、`build-studio-cpu`、`build-cuda-debug`。这些 preset 不执行依赖准备，`cuda-debug` 启用测试。调整准备 profile 时，可在配置命令后加入普通 CMake `-D` 选项，也可通过 `CMakeUserPresets.json` 保存本机选择。

<a id="source-archives-and-eos-tables"></a>
## 源码压缩包与 EOS 表

Sod 使用 IdealGas，无需 EOS 表。Helmholtz 和表格 EOS 算例需要 `EOS_toolkit/tables/` 中的相应数据。以 `version https://git-lfs.github.com/spec/v1` 开头的文件是 Git LFS 指针，不是表格数据。

Git checkout 只下载所需表格。完整 Runtime 安装包含以下三个文件：

```bash
git lfs pull --include="EOS_toolkit/tables/helmholtz/helm_table.dat,EOS_toolkit/tables/baryon/eos2.tab,EOS_toolkit/tables/baryon/eos4.tab" --exclude=""
```

该 Git 命令同样适用于 PowerShell。根据所选版本的指针（例如 `git show HEAD:EOS_toolkit/tables/helmholtz/helm_table.dat`）核对大小及 SHA-256，并保留[数据归属信息](../../THIRD_PARTY_NOTICES.zh-CN.md)。

源码 ZIP/tar 不含 Git 元数据。可从同一 tag／commit 的 checkout 复制已校验表格，或通过临时 checkout 获取。把 `YOUR_RELEASE_TAG_OR_COMMIT` 换成源码包对应版本。

Linux／WSL2：

```bash
git clone --no-checkout https://github.com/Shiro-Akane/ARCH.git ../ARCH-tables
GIT_LFS_SKIP_SMUDGE=1 git -C ../ARCH-tables checkout --detach YOUR_RELEASE_TAG_OR_COMMIT
git -C ../ARCH-tables lfs pull --include="EOS_toolkit/tables/helmholtz/helm_table.dat" --exclude=""
cp ../ARCH-tables/EOS_toolkit/tables/helmholtz/helm_table.dat EOS_toolkit/tables/helmholtz/
```

Windows PowerShell：

```powershell
git clone --no-checkout https://github.com/Shiro-Akane/ARCH.git ../ARCH-tables
$env:GIT_LFS_SKIP_SMUDGE = '1'
git -C ../ARCH-tables checkout --detach YOUR_RELEASE_TAG_OR_COMMIT
Remove-Item Env:GIT_LFS_SKIP_SMUDGE
git -C ../ARCH-tables lfs pull --include="EOS_toolkit/tables/helmholtz/helm_table.dat" --exclude=""
Copy-Item ../ARCH-tables/EOS_toolkit/tables/helmholtz/helm_table.dat EOS_toolkit/tables/helmholtz/
```

需要 baryon 表时以相同方式选择其路径。复制完成后可删除临时 checkout。

<a id="offline-tables-and-lfs-bandwidth"></a>
## 离线表格与 LFS 下载

将已验证表格保存在构建／输出目录和 Git LFS 缓存之外。所需 LFS 对象已在本机时，`git lfs checkout <path>` 可直接还原文件，无需下载。要关闭当前仓库及关联 worktree 的自动下载：

```bash
git config --local filter.lfs.smudge 'git-lfs smudge --skip -- %f'
git config --local filter.lfs.process 'git-lfs filter-process --skip'
git lfs checkout EOS_toolkit/tables/helmholtz/helm_table.dat
```

这些命令也适用于 PowerShell。缺少对象时，使用上面的 `git lfs pull --include=... --exclude=""` 只下载所需文件。

<a id="install-a-runtime-package"></a>
## 安装运行包

编译所选 profile 并准备上述三份 EOS 表后，安装 Runtime 组件。

Linux／WSL2：

```bash
cmake --install build/linux-core --prefix "$PWD/arch-install" --component Runtime
cd arch-install/share/arch
../../bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

Windows Command Prompt：

```bat
call build\windows-core\bin\arch-env.cmd
cmake --install build/windows-core --prefix "%CD%/arch-install" --component Runtime
cd arch-install\share\arch
..\..\bin\ARCH.exe Sod simulation/Sod/Sod_beginner.par
```

CUDA 使用对应构建目录。安装内容包括 ARCH、示例参数、EOS 表、许可证和 Windows 辅助程序。保持所选 HDF5、OpenMP 和 CUDA/cuDSS 运行库可用；相对输入路径仍以启动工作目录为基准。

<a id="parallel-compilation-and-memory"></a>
## 并行编译与内存

从 `ARCH_CUDA_HEAVY_COMPILE_JOBS=1` 和 `--parallel 1` 开始。Ninja 同时限制重型 CUDA／dispatch 编译池和总任务数；其他生成器不使用该任务池。CUDA 编译消耗宿主 CPU 和内存，空闲显存不能减少这些需求。更大的生成网络和更多 GPU 架构会增加编译工作。

Linux／WSL2 可使用内存保护器，在持续内存压力下停止本次构建：

```bash
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build/linux-core-with-cuda --target ARCH --parallel 1
```

保护器检查可用 RAM、swap 增量及 Linux PSI full-stall。默认条件是可用 RAM 少于 1.5 GiB、swap 比启动时增加超过 256 MiB，或内存／I/O full-stall 分别超过 20%／50% 持续 10 秒。它需要 `/proc`、PSI、Python `os.pidfd_open`／`signal.pidfd_send_signal` 和 Linux child-subreaper。在 `--` 前添加 `--log <新文件>` 保存测量；`Ctrl+C` 会停止本次构建及其子进程。

原生 Windows 使用保守任务数，并在任务管理器观察可用内存；Linux 保护器不在 Windows 上运行。有实际余量后再提高并行度。WSL 的 `free -h` 显示分配给 Linux 的内存，可能少于主机安装容量。

<a id="optimization-compilers-and-target-gpus"></a>
## 优化、编译器与目标 GPU

Release 使用所选编译器的优化及其支持的 LTO/IPO。GNU 构建使用 `-O3 -march=native`；MSVC 使用 Release 优化和 `/fp:strict`。共用浮点设置保留有限值检查、显式运算顺序和 CUDA 次正规数处理，关闭 fast-math 与隐式乘加融合。调整构建并行度无需改动这些设置。

`CMAKE_CUDA_ARCHITECTURES=native` 选择配置时可见的 GPU。面向另一台机器时可指定工具包支持的代码映像列表，例如 `-DCMAKE_CUDA_ARCHITECTURES="80;86;90"`；ARCH 使用同一列表进行启动检查。映像越多，编译越慢。GNU CPU 代码也通过 `-march=native` 面向构建机器，换用其他 CPU 架构时应重新编译。

CMake 找到 `ccache` 后会使用它。已有缓存可缩短重复构建；修改广泛引用的头文件仍会影响许多翻译单元。

<a id="build-output"></a>
## 构建输出

配置时显示所选后端、优化和依赖。需要依赖库的完整消息时添加 `--log-level=VERBOSE` 或设置 `ARCH_VERBOSE_BUILD=ON`；查看编译／链接命令则使用 `cmake --build <构建目录> --verbose`。Debug 控制编译设置，不控制日志详细程度。

<a id="build-tests-only-when-you-need-them"></a>
## 按需构建测试

`BUILD_TESTING=ON` 注册测试目标，编译完成后由 CTest 执行。两平台均可按以下方式运行一个小型测试：

```bash
cmake -S . -B build/linux-core -DBUILD_TESTING=ON
cmake --build build/linux-core --target arch_boundary_plan --parallel 1
ctest --test-dir build/linux-core -R '^boundary_plan$' --output-on-failure
```

Windows 将 `build/linux-core` 换成对应构建目录，并保留其编译环境。需要指定 Python 时添加 `-DPython3_EXECUTABLE=<绝对路径>`；科学 Python 检查需要 NumPy、h5py。CUDA 测试构建增加 NVCC 工作量；`--target ARCH` 只构建程序及依赖，不构建独立测试。

Linux 工具测试另需 `mold`、`shellcheck` 和提供 pidfd 的 Python。Ubuntu 24.04：

```bash
sudo apt-get install --no-install-recommends python3-numpy python3-h5py mold shellcheck
python3 -m unittest discover -s tests/tooling -p 'test_*.py' -v
```

目标名称与平台分组见[测试说明](../../tests/README.md)，科学复现见[验证指南](../../validation/README.md)。算例专属 `physical_boundary.cpp` 和 `gravity_boundary.cpp` 与注册算例一起编译，详见[用户边界](UserBoundaries.zh-CN.md)。

<a id="troubleshooting"></a>
## 常见问题

| 现象 | 检查内容 |
| --- | --- |
| CMake 选中其他编译器或库 | 新建构建目录，明确编译器路径和同 ABI HDF5/KLU 前缀 |
| Windows 命令链或资源编译失败 | `ComSpec` 使用原生反斜杠，`PATHEXT` 包含 `.EXE`，PATH 包含所选 `rc.exe`／`windres.exe` |
| MSVC 资源依赖错误 | 使用含 `cmcldeps.exe` 的完整 Kitware CMake |
| Windows 缺少 DLL | CLI 先调用所选构建的 `arch-env.cmd`；移动依赖后重新生成 Studio 工具配置 |
| Unicode 构建路径下 GNU LTO 失败 | 源码、构建和 LTO 临时目录改用短 ASCII 路径 |
| Linux Electron 无法打开 | 检查图形会话／WSLg、`DISPLAY` 及 `ldd studio/node_modules/electron/dist/electron` 的缺库项 |
| Windows Run 无法打开终端 | 检查原生终端；自定义 Windows Terminal 别名可通过可信工具配置指定 |
| Studio 显示 build freshness unknown | MSVC/CUDA 等工具链的完整依赖覆盖不可用；保留明确的构建选择，并查看 Build 诊断 |
| Preview 超过资源限额 | 减小采样或初始 AMR 规模；Windows 使用 1024 MiB committed-memory 与用户 CPU 时间，Linux 使用地址空间与进程 CPU 限额 |
| EOS 表只有少量 Git LFS 指针文本 | 使用对应 EOS 前先还原匹配的 Git LFS 表格 |

UCRT64/GCC 16.2.0 的 Plotfile 测试入口在 CMake 中具有特定 IPO 兼容设置，生产目标仍保留 IPO。EOS 文本解析遵循宿主库的可表示范围：MSVC 拒绝 `1e-500` 等低于 binary64 范围的已测字面值，Linux 提取器则接受带符号零。使用提供的规范表格及可表示数值。
