# 构建 ARCH

[English](Build.md) · [快速开始](../../README.zh-CN.md#构建) · [Studio](Studio.zh-CN.md)

ARCH 在 Linux、WSL2 和原生 Windows 11 x64 上使用同一套 CMake 工程。**推荐使用 Linux 进行科学计算。** Windows 在已说明的[功能范围](../Features.zh-CN.md)内提供相同的 CPU、CUDA 与 Studio 功能，但平台适配较新，可能仍有不稳定性或 bug。Studio 为可选组件；命令行计算不需要 Node 或 Electron。

先选择下方 CPU 或 CUDA 路线，需要桌面时再添加 Studio。Linux 示例使用 Bash；Windows 示例使用 PowerShell，另行标为 **Developer Command Prompt** 的命令使用 CMD。在 ARCH 根目录执行项目命令。不同操作系统、编译器 ABI 使用独立构建目录和依赖安装前缀。

<a id="tools-and-dependencies"></a>
## 工具与依赖

| 组件 | Linux／WSL2 | 原生 Windows 11 x64 |
| --- | --- | --- |
| CPU 编译器 | 支持 C++20 的 GCC 或 Clang；配套 C/C++ 工具链 | MSYS2 **UCRT64** GCC 与对应 libgomp |
| CUDA 编译器 | NVCC 与兼容的 GCC/Clang 宿主编译器 | NVCC、MSVC x64、Visual Studio C++ 公共工具及 Windows SDK |
| 构建工具 | Git、Ninja；CPU 使用 CMake 3.22+，CUDA 使用 3.25.2+ | 原生 Git、Ninja；GCC 使用 UCRT64 CMake，MSVC 使用完整 Kitware Windows CMake |
| 必需库 | HDF5 C++/HL、OpenMP | 与所选编译器 ABI 一致的 HDF5 C++/HL、OpenMP |
| 共用头文件／CPU 稀疏求解器 | HighFive 2.9.0；已安装 KLU 或 SuiteSparse 7.13.0 | 相同版本，但 GCC 与 MSVC 分别编译库 |
| CUDA 运行环境 | CUDA Toolkit 12.0+、兼容的 NVIDIA 驱动 | Windows CUDA Toolkit、兼容的 NVIDIA 驱动 |
| CUDA 稀疏燃烧 | 可选 cuDSS **0.8**，匹配 CUDA 主版本 | cuDSS 0.8 DLL／导入库及匹配的 cuBLAS/cuBLASLt DLL |
| 可选桌面 | 原生 x64 Node 24+、npm、Electron、图形会话 | 原生 x64 Node 24+、npm、Electron、Windows 桌面 |
| 测试工具 | Python 3.10+；科学检查另需 NumPy、h5py | 原生 Python 3.10+；科学检查另需 NumPy、h5py |

首次配置时，CMake 下载 HighFive 及缺少的 SuiteSparse；也可指定本地源码。版本定义见 [Dependencies.cmake](../../cmake/dependencies/Dependencies.cmake)。OpenMP 和 KLU 默认启用；`ARCH_ENABLE_OPENMP=OFF` 关闭 CPU 线程并行，`ARCH_ENABLE_KLU=OFF` 移除 CPU 稀疏求解器。

已测试组合包括 Linux CUDA 12.3/GCC 12、Windows CPU GCC 16.2.0/CMake 4.4.4，以及 Windows CUDA 12.9.86/MSVC 19.44.35229（工具集 14.44）/SDK 10.0.26100/CMake 3.31.11。Windows 依赖采用 HDF5 1.14.6 和 SuiteSparse 7.13.0。MSYS2 滚动更新，当前安装包可能是其他版本。

下方 Windows CUDA 配方使用已测试的 MSVC 14.44；14.38 不能通过现有 NaN 契约检查。NVIDIA 的 [CUDA 12.9 编译器表](https://docs.nvidia.com/cuda/archive/12.9.1/cuda-installation-guide-microsoft-windows/index.html)列出 193x 编译器系列，上述 14.44 组合是 ARCH 另行测试的配置。选择其他版本时核对工具链兼容性，不使用 `--allow-unsupported-compiler`。

<a id="linux--wsl2-installation"></a>
### Linux／WSL2 安装

Ubuntu 24.04：

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends \
  build-essential cmake ninja-build git git-lfs libhdf5-dev
```

CUDA 构建还需安装 NVIDIA Linux CUDA Toolkit 及其兼容宿主编译器。WSL2 的 GPU 驱动安装在 Windows，WSL 内的工具包按 [NVIDIA WSL 指南](https://docs.nvidia.com/cuda/wsl-user-guide/index.html)安装。

<a id="windows-cpu-installation"></a>
### Windows CPU 安装

将 [MSYS2](https://www.msys2.org/docs/installer/) 安装在 `C:/msys64` 等较短本地路径。在 **UCRT64** 终端更新并安装工具：

```bash
pacman -Syu
# 若更新要求关闭终端，重新打开 UCRT64 后继续。
pacman -Syu --needed mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-libgomp mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-zlib
```

剩余 Windows 命令使用原生 PowerShell。安装原生 Git，再取得所需上游源码。将示例路径替换为本机位置：

```powershell
$repo = 'C:/src/ARCH'
$ucrt = 'C:/msys64/ucrt64'
$sources = 'C:/src/arch-dependencies'
$deps = 'C:/tools/arch-ucrt-deps'
New-Item -ItemType Directory -Force $sources | Out-Null
git clone --depth 1 --branch hdf5_1.14.6 https://github.com/HDFGroup/hdf5.git "$sources/hdf5"
git clone --depth 1 --branch v2.9.0 https://github.com/BlueBrain/HighFive.git "$sources/highfive"
git clone --depth 1 --branch v7.13.0 https://github.com/DrTimothyAldenDavis/SuiteSparse.git "$sources/suitesparse"
$env:Path = "$deps/bin;$ucrt/bin;$env:SystemRoot\System32;$env:SystemRoot"
$env:ComSpec = Join-Path $env:SystemRoot 'System32\cmd.exe'
$env:PATHEXT = '.COM;.EXE;.BAT;.CMD'
$common = @('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
  "-DCMAKE_C_COMPILER:FILEPATH=$ucrt/bin/gcc.exe",
  "-DCMAKE_CXX_COMPILER:FILEPATH=$ucrt/bin/g++.exe",
  "-DCMAKE_MAKE_PROGRAM:FILEPATH=$ucrt/bin/ninja.exe",
  "-DCMAKE_INSTALL_PREFIX:PATH=$deps", '-DCMAKE_CXX_STANDARD=20',
  '-DCMAKE_POLICY_VERSION_MINIMUM=3.5')
```

使用同一编译器构建启用 C++/HL 和 zlib 的 HDF5，以及最小 KLU 依赖集。每条命令成功后再继续；PowerShell 可用 `$LASTEXITCODE` 查看退出状态。

```powershell
cmake -S "$sources/hdf5" -B "$sources/build-hdf5-ucrt" @common `
  -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF -DBUILD_TESTING=OFF `
  -DHDF5_BUILD_CPP_LIB=ON -DHDF5_BUILD_HL_LIB=ON -DHDF5_BUILD_FORTRAN=OFF `
  -DHDF5_BUILD_TOOLS=OFF -DHDF5_BUILD_EXAMPLES=OFF -DHDF5_ENABLE_PARALLEL=OFF `
  -DHDF5_ENABLE_Z_LIB_SUPPORT=ON -DHDF5_ENABLE_SZIP_SUPPORT=OFF `
  -DHDF5_ENABLE_NONSTANDARD_FEATURE_FLOAT16=OFF "-DZLIB_ROOT:PATH=$ucrt"
cmake --build "$sources/build-hdf5-ucrt" --parallel 2
cmake --install "$sources/build-hdf5-ucrt"

cmake -S "$sources/suitesparse" -B "$sources/build-klu-ucrt" @common `
  '-DSUITESPARSE_ENABLE_PROJECTS=klu;amd;colamd' -DKLU_USE_CHOLMOD=OFF `
  -DSUITESPARSE_USE_CUDA=OFF -DSUITESPARSE_USE_FORTRAN=OFF -DSUITESPARSE_REQUIRE_BLAS=OFF `
  -DBLA_VENDOR=Generic -DSUITESPARSE_USE_OPENMP=OFF -DSUITESPARSE_USE_PYTHON=OFF `
  -DSUITESPARSE_DEMOS=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON
cmake --build "$sources/build-klu-ucrt" --parallel 2
cmake --install "$sources/build-klu-ucrt"
```

HighFive 为纯头文件库，ARCH 可直接使用其源码目录。该 GCC 配置缺少 `FLT16_MAX`，因此关闭 HDF5 的可选 `_Float16` 扩展。CMake policy 设置用于让固定版本依赖适配 CMake 4。本构建的 PATH 不应混入 MSYS `usr/bin`、Anaconda 库或其他 C++ 运行时。

<a id="windows-cuda-installation"></a>
### Windows CUDA 安装

安装 Visual Studio 2022 Build Tools 的 C++ x64/x86 工具、MSVC 14.44 和 Windows SDK。保留包含 `VC/Auxiliary/Build/vcvarsall.bat` 的 VC 公共工具，供 NVCC 使用。另装完整的 [Kitware Windows CMake](https://cmake.org/download/)、原生 Ninja 及匹配的 Windows CUDA Toolkit。MSVC 资源依赖跟踪需要 Kitware 的 `cmcldeps.exe`。

HDF5 和 KLU 使用 `C:/tools/arch-msvc-deps` 等独立前缀，不能复用 GCC/UCRT64 库。以下 **Developer Command Prompt** 示例初始化所选工具集，并使用前面取得的源码构建 HDF5：

```bat
call "C:\Tools\VS2022\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -vcvars_ver=14.44
set "ARCH_CMAKE=C:\Tools\cmake"
set "ARCH_CUDA=C:\Tools\cuda-12.9"
set "ARCH_MSVC_DEPS=C:\Tools\arch-msvc-deps"
set "PATH=%ARCH_CMAKE%\bin;C:\Tools\ninja;%ARCH_CUDA%\bin;%ARCH_MSVC_DEPS%\bin;%PATH%"
cmake -S C:/src/arch-dependencies/hdf5 -B C:/src/arch-dependencies/build-hdf5-msvc -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe ^
  "-DCMAKE_INSTALL_PREFIX=%ARCH_MSVC_DEPS%" ^
  -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF -DBUILD_TESTING=OFF ^
  -DHDF5_BUILD_CPP_LIB=ON -DHDF5_BUILD_HL_LIB=ON -DHDF5_BUILD_FORTRAN=OFF ^
  -DHDF5_BUILD_TOOLS=OFF -DHDF5_BUILD_EXAMPLES=OFF -DHDF5_ENABLE_PARALLEL=OFF ^
  -DHDF5_ENABLE_Z_LIB_SUPPORT=OFF -DHDF5_ENABLE_SZIP_SUPPORT=OFF ^
  -DHDF5_ENABLE_NONSTANDARD_FEATURE_FLOAT16=OFF
cmake --build C:/src/arch-dependencies/build-hdf5-msvc --parallel 2
cmake --install C:/src/arch-dependencies/build-hdf5-msvc
```

ARCH 的 SuiteSparse 配置会用同一 MSVC 编译器构建 KLU。保留 Developer 终端中的 Git，以便下载依赖；也可通过 `CMAKE_PREFIX_PATH` 选用已安装的 MSVC KLU。

ARCH 在 MSVC 上使用 `/openmp:llvm /openmp:experimental`。将所选工具集里包含 `libomp140.x86_64.dll` 的 `Microsoft.VC143.OpenMP.LLVM` 目录加入运行 PATH；它通常位于 `VC/Redist/MSVC/<安装版本>/debug_nonredist/x64/`。该 [Microsoft OpenMP 运行时](https://learn.microsoft.com/en-us/cpp/build/reference/openmp-enable-openmp-2-0-support?view=msvc-170)不可再分发，由使用者的工具链安装提供。

<a id="configure-first-then-compile"></a>
## 先配置，再编译

配置阶段查找编译器和库，写入 `CMakeCache.txt` 并生成 Ninja 指令；编译阶段生成可执行程序。修改源码后重复构建命令，修改选项后先重新配置。切换操作系统、编译器或生成器时使用新的构建目录。

<a id="cpu-build"></a>
### CPU 构建

Linux／WSL2：

```bash
cmake --preset cpu-release
cmake --build build-cpu --target ARCH --parallel 1
```

Windows PowerShell，沿用前面的 UCRT64 变量与 PATH：

```powershell
Set-Location $repo
$build = "$repo/build-windows-cpu"
cmake -S . -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_C_COMPILER:FILEPATH=$ucrt/bin/gcc.exe" `
  "-DCMAKE_CXX_COMPILER:FILEPATH=$ucrt/bin/g++.exe" `
  "-DCMAKE_RC_COMPILER:FILEPATH=$ucrt/bin/windres.exe" `
  "-DCMAKE_PREFIX_PATH:PATH=$deps" "-DHDF5_ROOT:PATH=$deps" `
  "-DFETCHCONTENT_SOURCE_DIR_HIGHFIVE:PATH=$sources/highfive" `
  -DARCH_ENABLE_CUDA=OFF -DARCH_ENABLE_OPENMP=ON `
  -DARCH_ENABLE_KLU=ON -DARCH_FETCH_SUITESPARSE=OFF `
  -DARCH_BUILD_STUDIO=OFF -DBUILD_TESTING=OFF `
  "-DARCH_RUNTIME_OUTPUT_DIRECTORY:PATH=$build/bin"
cmake --build $build --target ARCH --parallel 1
```

产物分别是 `build-cpu/bin/ARCH` 和 `build-windows-cpu/bin/ARCH.exe`。`ARCH_RUNTIME_OUTPUT_DIRECTORY` 指定输出位置；未设置时输出到源码树的 `bin/`。

<a id="native-windows-cpu-and-cuda"></a>
<a id="cuda-build"></a>
### CUDA 构建

Linux／WSL2：

```bash
cmake --preset cuda-release
cmake --build build-cuda --target ARCH --parallel 1
```

GCC 的 C 与 C++ 编译器使用相同主版本，CUDA 宿主编译器也从同一安装选择。在首次配置时指定，例如 `-DCMAKE_C_COMPILER=/usr/bin/gcc-12 -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-12`。

Windows **Developer Command Prompt**，先完成前面的编译环境与 HDF5 安装：

```bat
cd /d C:\src\ARCH
cmake -S . -B build-cuda-win -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe ^
  "-DCMAKE_CUDA_COMPILER=%ARCH_CUDA%/bin/nvcc.exe" ^
  "-DCMAKE_RC_COMPILER=C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe" ^
  "-DCUDAToolkit_ROOT=%ARCH_CUDA%" -DCMAKE_CUDA_ARCHITECTURES=native ^
  "-DCMAKE_PREFIX_PATH=%ARCH_MSVC_DEPS%" "-DHDF5_ROOT=%ARCH_MSVC_DEPS%" ^
  "-DFETCHCONTENT_SOURCE_DIR_HIGHFIVE=C:/src/arch-dependencies/highfive" ^
  -DARCH_ENABLE_CUDA=ON -DARCH_ENABLE_OPENMP=ON ^
  -DARCH_CUDA_HEAVY_COMPILE_JOBS=1 -DBUILD_TESTING=OFF ^
  "-DARCH_RUNTIME_OUTPUT_DIRECTORY=%CD%/build-cuda-win/bin"
cmake --build build-cuda-win --target ARCH --parallel 1
```

SDK 路径需匹配安装版本。Windows CUDA 使用共享 `cudart`；运行 PATH 需包含 CUDA DLL、同 ABI HDF5、已安装的 LLVM OpenMP 运行时，以及所用的 cuDSS/cuBLAS。

Linux CUDA preset 输出 `build-cuda/bin/ARCH`，Windows 示例输出 `build-cuda-win/bin/ARCH.exe`。两者均可运行 CPU 或 CUDA 算例。`.par` 的 `compute_backend = cpu`、`cuda` 或 `auto` 在启动计算时选择后端；CPU-only 程序不能仅通过参数启用 CUDA。GPU 目标与编译内存选项见下文。

<a id="presets-and-build-options"></a>
### Preset 与构建选项

`cmake --list-presets` 列出 [CMakePresets.json](../../CMakePresets.json) 中的共用配方。配合对应平台的编译器／依赖环境使用，或使用上面的显式参数。

| Preset | 目录 | 用途 |
| --- | --- | --- |
| `cpu-release` | `build-cpu` | CPU 程序 |
| `studio-cpu-release` | `build-studio-cpu` | GNU CPU 程序和桌面，记录构建输入 |
| `cuda-release` | `build-cuda` | 面向可见 GPU 的 CPU/CUDA 程序 |
| `cuda-debug` | `build-cuda-debug` | 启用测试的 CUDA 开发构建 |

Release preset 不构建测试，`cuda-debug` 启用测试。各 preset 均使用 Ninja 和 OpenMP。`studio-cpu-release` 启用 GNU 链接依赖记录和 LTO 输入保留；MSVC CUDA 桌面在现有 CUDA 树中启用 Studio，见下文。`ARCH_BUILD_STUDIO` 默认关闭。

<a id="run-and-select-paths"></a>
## 运行与路径

Linux／WSL2：

```bash
export OMP_NUM_THREADS=4
./build-cpu/bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

Windows PowerShell：

```powershell
$env:OMP_NUM_THREADS = '4'
& .\build-windows-cpu\bin\ARCH.exe Sod simulation/Sod/Sod_beginner.par
```

使用 CUDA 时替换为对应程序，并在输入副本中修改已有的 `compute_backend` 项；`cuda_device` 选择 GPU 序号。运行日志和 `*_backend_plan.txt` 会记录所选后端。

两平台共用 `.par` 格式。表格、输出和续算文件的相对路径基于**计算的工作目录**，不是参数文件所在目录。Studio 使用项目根目录。优先采用 `out_dir = output/first_sod` 等相对值；绝对路径必须对应执行程序的系统。`/mnt/c/src/ARCH` 属于 WSL，`C:/src/ARCH` 属于 Windows，仅替换斜杠不能转换两种路径。

`.par` 保存为 UTF-8。Windows 程序携带进程级 UTF-8 manifest，无需修改系统区域设置。GNU LTO 的源码／构建目录使用短 ASCII 路径；普通本地输入／输出路径支持空格和 Unicode。UNC／网络文件系统与任意长路径不属于已测配置。Studio 的项目相对路径在两平台均使用 `/`，见 [Studio 路径说明](Studio.zh-CN.md#linux-与-windows-的路径)。

<a id="studio-desktop"></a>
<a id="linux-studio-desktop"></a>
## Studio 桌面

Studio 需要原生 x64 Node **24+**、npm 和锁定版本的 Electron。可使用官方 Node 分发包或已有 Node 管理器；打包时保留 Node 的许可证。Node、Electron、构建缓存和 `studio/node_modules` 属于对应宿主系统；在两个系统构建桌面时使用各自的源码副本。

Linux／WSL2 还需要图形会话（WSL 使用 WSLg）及 Electron 系统库。Ubuntu 24.04：

```bash
sudo apt-get install --no-install-recommends \
  libgtk-3-0t64 libnss3 libgbm1 libasound2t64 libx11-xcb1 libxkbcommon0 \
  xterm util-linux
```

Run/Restart 打开独立终端：Linux 使用 `/usr/bin/xterm` 与 `/usr/bin/flock`，Windows 使用 Windows Terminal。编辑和 Preview 不需要该终端。

<a id="build-and-launch-the-desktop"></a>
### 构建并启动桌面

Linux／WSL2，先将原生 Node 24+ 加入 PATH：

```bash
node --version
cmake --preset studio-cpu-release -DARCH_STUDIO_NODE="$(command -v node)"
cmake --build build-studio-cpu --parallel 1
build-studio-cpu/bin/arch-studio --project "$PWD" --build-dir build-studio-cpu \
  --binary build-studio-cpu/bin/ARCH --case Sod --config simulation/Sod/Sod_beginner.par
```

Windows 在前面配置的 CPU 或 CUDA 树中启用 Studio。在 PowerShell 中将 `$build` 和 `$cmake` 指向对应构建树与工具链；CUDA 用户从已初始化的 Developer Command Prompt 启动 PowerShell，以继承编译环境。

```powershell
$build = 'C:/src/ARCH/build-windows-cpu'
$cmake = 'C:/msys64/ucrt64/bin/cmake.exe'
$node = 'C:/tools/node/node.exe'
& $cmake -S . -B $build -DARCH_BUILD_STUDIO=ON "-DARCH_STUDIO_NODE:FILEPATH=$node" `
  -DARCH_EMIT_LINK_DEPENDENCIES=ON -DARCH_RETAIN_LTO_LINK_INPUTS=ON
& $cmake --build $build --target arch-studio arch_windows_process arch_windows_build_publish --parallel 1
```

两个 GNU 选项记录 CPU Studio Build 检查所需的链接输入。CUDA 在现有 MSVC 树中配置，不添加这两个选项：

```powershell
$build = 'C:/src/ARCH/build-cuda-win'
$cmake = 'C:/Tools/cmake/bin/cmake.exe'
$node = 'C:/tools/node/node.exe'
& $cmake -S . -B $build -DARCH_BUILD_STUDIO=ON "-DARCH_STUDIO_NODE:FILEPATH=$node"
& $cmake --build $build --target arch-studio arch_windows_process arch_windows_build_publish --parallel 1
```

两平台使用相同目标：`arch-studio-dependencies` 根据包清单与 Node 检查共用的 npm 安装；这些输入变更或整个 `node_modules` 被删除时才安装依赖。`arch-studio-assets` 构建界面，普通源码修改会复用已有依赖。`arch-studio-runtime` 准备 Electron。Windows 上 CMake 调用 `npm-cli.js`，因此无需调整 PowerShell 的 `npm.ps1` 执行策略。

<a id="windows-tool-configuration"></a>
### Windows 工具配置

在启动终端设置 `ARCH_STUDIO_WINDOWS_TOOLS`，使 Studio 找到所选编译工具、DLL 和原生进程／文件辅助程序。各路径必须为本地绝对路径。以下是完整的 GCC/UCRT64 示例：

```powershell
$repo = 'C:/src/ARCH'
$build = "$repo/build-windows-cpu"
$ucrtBin = 'C:/msys64/ucrt64/bin'
$deps = 'C:/tools/arch-ucrt-deps'
$tools = @{
  cmake = "$ucrtBin/cmake.exe"
  supervisor = "$build/arch_windows_process.exe"
  ninja = "$ucrtBin/ninja.exe"
  objdump = "$ucrtBin/objdump.exe"
  publisher = "$build/arch_windows_build_publish.exe"
  rc = "$ucrtBin/windres.exe"
  terminal = (Join-Path $env:LOCALAPPDATA 'Microsoft/WindowsApps/wt.exe')
  path = "$deps/bin;$ucrtBin;$env:SystemRoot/System32;$env:SystemRoot"
}
$env:ARCH_STUDIO_WINDOWS_TOOLS = $tools | ConvertTo-Json -Depth 4 -Compress
& "$build/bin/arch-studio.cmd" --project $repo --build-dir build-windows-cpu `
  --binary build-windows-cpu/bin/ARCH.exe --case Sod --config simulation/Sod/Sod_beginner.par
```

Windows Terminal 别名位置不同时调整其路径。这些设置属于本机启动环境，不写入 `.par`。源码入口使用配置时选定的 Node；便携包使用随包 Node，也可通过 `ARCH_STUDIO_NODE` 显式指定其他运行时。

**MSVC/CUDA** 用户从已初始化的 Developer Command Prompt 启动 PowerShell，再将工具配置替换为下例。调整各安装路径，尤其是 OpenMP 运行库目录及 SDK `rc.exe`；后者须与选定构建树的 `CMAKE_RC_COMPILER` 相同。

```powershell
$repo = 'C:/src/ARCH'
$build = "$repo/build-cuda-win"
$vc = Split-Path (Get-Command cl.exe).Source
$sdk = 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64'
$omp = 'C:/Tools/VS2022/VC/Redist/MSVC/14.44.35112/debug_nonredist/x64/Microsoft.VC143.OpenMP.LLVM'
$tools = @{
  cmake = 'C:/Tools/cmake/bin/cmake.exe'
  supervisor = "$build/arch_windows_process.exe"
  ninja = 'C:/Tools/ninja/ninja.exe'
  objdump = 'C:/msys64/ucrt64/bin/objdump.exe'
  publisher = "$build/arch_windows_build_publish.exe"
  rc = "$sdk/rc.exe"
  terminal = (Join-Path $env:LOCALAPPDATA 'Microsoft/WindowsApps/wt.exe')
  path = "$vc;$sdk;C:/Tools/arch-msvc-deps/bin;C:/Tools/cuda-12.9/bin;$omp;C:/msys64/ucrt64/bin;$env:SystemRoot/System32;$env:SystemRoot"
  compilerEnvironment = @{
    INCLUDE = (($env:INCLUDE -split ';' | Where-Object { $_ }) -join ';')
    LIB = (($env:LIB -split ';' | Where-Object { $_ }) -join ';')
    LIBPATH = (($env:LIBPATH -split ';' | Where-Object { $_ }) -join ';')
  }
}
$env:ARCH_STUDIO_WINDOWS_TOOLS = $tools | ConvertTo-Json -Depth 4 -Compress
& "$build/bin/arch-studio.cmd" --project $repo --build-dir build-cuda-win `
  --binary build-cuda-win/bin/ARCH.exe --case Sod --config simulation/Sod/Sod_beginner.par
```

使用 CUDA 稀疏燃烧时，在 `tools.path` 中加入 cuDSS 的 `bin` 目录。`objdump.exe` 用于检查 PE 文件，不会把 GCC 选作 CUDA 宿主编译器。`INCLUDE`、`LIB`、`LIBPATH` 传递 MSVC 的头文件和库搜索路径。

<a id="package-the-desktop"></a>
### 打包桌面

在目标操作系统上构建便携目录包：

```bash
cmake --build build-studio-cpu --target arch-studio-package --parallel 1
```

Windows 在对应编译环境中使用相同命令，将构建目录换为 `build-windows-cpu` 或 `build-cuda-win`。输出位于 `studio/.local/desktop-release/`：

| 系统 | 目录 | 入口 |
| --- | --- | --- |
| Linux | `arch-studio-bin-linux-x64` | `arch-studio` |
| Windows | `arch-studio-bin-win32-x64` | `arch-studio-bin.exe` |

保持整个目录完整。包内包含 Electron、独立 Host Node、h5wasm 和许可证；Core、其编译工具／运行库及独立终端由外部环境提供。Windows 便携入口启动前也需设置上述工具配置。生成的 `bin/arch-studio` 或 `.cmd` 源码入口则依赖原 checkout。

打包器从 `package.json` 读取 Electron 的精确版本，并核对锁文件和已安装包。`electron_config_cache` 可指定含官方、校验和匹配的 Electron 压缩包的离线缓存。保留 Node、Electron/Chromium、h5wasm/HDF5 许可证。当前打包产物为便携目录，未配置签名、安装器或自动更新。

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

构建 ARCH 后，先还原前述三个表格文件。Windows 还需执行 `cmake --build <构建目录> --target arch_windows_process arch_windows_build_publish` 构建原生辅助程序，再安装 Runtime 组件。

Linux／WSL2：

```bash
cmake --install build-cpu --prefix "$PWD/arch-install" --component Runtime
cd arch-install/share/arch
../../bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

Windows PowerShell：

```powershell
cmake --install build-windows-cpu --prefix "$PWD/arch-install" --component Runtime
Set-Location arch-install/share/arch
& ../../bin/ARCH.exe Sod simulation/Sod/Sod_beginner.par
```

CUDA 程序使用对应 CUDA 构建目录。安装内容包含 ARCH、示例输入、EOS 表、许可证及 Windows 辅助程序；HDF5、OpenMP、CUDA/cuDSS 运行库仍须在所选环境中可用。相对路径继续基于启动时的工作目录。

<a id="cudss-and-sparse-burning"></a>
## cuDSS 与稀疏燃烧

`ARCH_ENABLE_CUDSS=ON` 在 CUDA 构建中查找 cuDSS **0.8**。安装前缀应包含 `include/cudss.h` 和 `lib/` 或 `lib64/`；Windows 另需导入库及 `bin/cudss64_0.dll`。例如：

```bash
cmake -S . -B build-cuda -DCUDSS_ROOT="$HOME/.local/cudss"
```

Windows PowerShell，使用 MSVC 环境：

```powershell
cmake -S . -B build-cuda-win -DCUDSS_ROOT:PATH=C:/Tools/cudss-0.8
```

Windows 运行 PATH 加入 cuDSS `bin` 及匹配 CUDA 的 cuBLAS/cuBLASLt DLL 目录。CMake 会显示选中的库。未找到 cuDSS 或设置 `ARCH_ENABLE_CUDSS=OFF` 时，紧凑 DenseLU 燃烧及其他 CUDA 功能仍可用，需要稀疏提供方的路线则报告配置不受支持。CPU 稀疏燃烧保留 KLU。所选 network/EOS 路线也必须编译，见[生成反应网络](../../validation/network/README.md)。

<a id="parallel-compilation-and-memory"></a>
## 并行编译与内存

从 `ARCH_CUDA_HEAVY_COMPILE_JOBS=1` 和 `--parallel 1` 开始。Ninja 同时限制重型 CUDA／dispatch 编译池和总任务数；其他生成器不使用该任务池。CUDA 编译消耗宿主 CPU 和内存，空闲显存不能减少这些需求。更大的生成网络和更多 GPU 架构会增加编译工作。

Linux／WSL2 可使用内存保护器，在持续内存压力下停止本次构建：

```bash
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-cuda --target ARCH --parallel 1
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
cmake -S . -B build-cpu -DBUILD_TESTING=ON
cmake --build build-cpu --target arch_boundary_plan --parallel 1
ctest --test-dir build-cpu -R '^boundary_plan$' --output-on-failure
```

Windows 将 `build-cpu` 换成对应构建目录，并保留其编译环境。需要指定 Python 时添加 `-DPython3_EXECUTABLE=<绝对路径>`；科学 Python 检查需要 NumPy、h5py。CUDA 测试构建增加 NVCC 工作量；`--target ARCH` 只构建程序及依赖，不构建独立测试。

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
| Windows 缺少 DLL | PATH 加入所选 ABI 的 HDF5、编译器／OpenMP、CUDA 和可选 cuDSS 运行库目录 |
| Unicode 构建路径下 GNU LTO 失败 | 源码、构建和 LTO 临时目录改用短 ASCII 路径 |
| Linux Electron 无法打开 | 检查图形会话／WSLg、`DISPLAY` 及 `ldd studio/node_modules/electron/dist/electron` 的缺库项 |
| Windows Run 无法打开终端 | 安装 Windows Terminal，在工具配置中填写其 `wt.exe` 别名绝对路径 |
| Studio 显示 build freshness unknown | MSVC/CUDA 等工具链的完整依赖覆盖不可用；保留明确的构建选择，并查看 Build 诊断 |
| Preview 超过资源限额 | 减小采样或初始 AMR 规模；Windows 使用 1024 MiB committed-memory 与用户 CPU 时间，Linux 使用地址空间与进程 CPU 限额 |
| HDF5 数据只有少量文本 | 使用对应 EOS 前先还原匹配的 Git LFS 表格 |

UCRT64/GCC 16.2.0 的 Plotfile 测试入口在 CMake 中具有特定 IPO 兼容设置，生产目标仍保留 IPO。EOS 文本解析遵循宿主库的可表示范围：MSVC 拒绝 `1e-500` 等低于 binary64 范围的已测字面值，Linux 提取器则接受带符号零。使用提供的规范表格及可表示数值。
