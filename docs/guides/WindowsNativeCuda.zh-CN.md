# Windows 原生 CUDA

本轮 Windows CPU／CUDA／Studio 对应功能已实现，并在实测配置与既有科学范围内完成验收；仍保留完整依赖新鲜度 unknown、独立控制台兜底未验收、无新增人工对话框／正常关闭声称等限制。[最终功能对照](../histroy/build/WindowsNativeCudaQualification-20261027.zh-CN.md)列出26类对应关系与验收证据；既有 CPU／Studio 指南继续适用。

推荐 Linux 作为主要科研运行环境。Windows 与 Linux 共用求解器、数学实现、参数格式和 HDF5 格式；Windows 适配较新，仍可能有不稳定性或 bug。

## 与 Linux 共用的构建边界

下表针对 CUDA；Windows CPU 另按 [GCC/UCRT64 同 ABI HDF5／KLU 指南](WindowsNativeCpu.zh-CN.md)准备，不得与 MSVC 依赖混用。

| 项目 | Linux／WSL2 CUDA | 原生 Windows CUDA（MSVC） |
|---|---|---|
| NVCC 宿主 | NVIDIA 支持的 GCC／Clang 组合 | NVIDIA 支持的 MSVC；不能复用 GCC/UCRT64 对象或依赖 |
| 工具 | CMake、Ninja、Linux CUDA Toolkit | 完整的 Kitware Windows CMake、Ninja、MSVC C++ 工具与 Windows SDK、Windows CUDA Toolkit |
| HDF5／KLU | 与宿主编译器兼容的库 | 用同一 MSVC 构建的 HDF5 C++／HL、KLU；HighFive 共用固定版本头文件 |
| 可选稀疏库 | cuDSS 0.8 与匹配 CUDA 主版本 | 相同 0.8 API、匹配 CUDA 主版本的 DLL 与导入库 |
| 运行输入 | 共用 `.par` 格式与规范 EOS 数据 | 调整绝对路径；可表示测试值保留原始位检查。MSVC 拒绝3个低于 binary64 范围的 EOS 字符串且不发布零，见下文读取边界 |

参考 Linux 构建为 NVCC12.3／GCC12。已验收的 Windows 配置为 NVCC12.9.86、MSVC19.44.35229（14.44）、SDK10.0.26100、CMake3.31.11、HDF51.14.6、SuiteSparse7.13.0、cuDSS0.8，GPU 为 RTX3060Ti（SM86）。其他工具链与硬件组合应重新验证。

MSVC14.38 虽能链接，但未通过未改动的 MC limiter NaN 判据；14.44 在 `/O2 /fp:strict` 下保留原行为，并通过下文完整原生验收，全程不用 `--allow-unsupported-compiler`。NVIDIA 的 CUDA12.9 表明确列出193x，应区分本轮额外实测配置与官方列举范围。A12–A18 仅按批准清单进行兼容调整，公式、阈值、拒绝检查与池行为不变；下方命令复现实测14.44环境。

NVCC 的宿主选择应遵循 [NVIDIA Windows 编译器支持表](https://docs.nvidia.com/cuda/archive/12.9.1/cuda-installation-guide-microsoft-windows/index.html)。不使用 `--allow-unsupported-compiler`。cuDSS 保留项目已审核的 [0.8 API](https://docs.nvidia.com/cuda/cudss/0.8.0/getting_started.html)，不能直接换成 1.0。

## 准备原生工具

1. 安装 Visual Studio 2022 Build Tools 的 C++ x64/x86 通用工具、受支持版本的 MSVC 和 Windows SDK。使用实测14.44工具集时，也需保留通用 VC 工具组件；只有侧装编译器而没有 `VC/Auxiliary/Build/vcvarsall.bat` 时，NVCC 可能无法识别宿主。
2. 使用 [Kitware 官方 Windows 发行包](https://cmake.org/download/)。本轮原有 MSYS2 CMake 缺少 `cmcldeps.exe`，不能直接用于 MSVC 的资源依赖编译；不通过关闭依赖记录规避。
3. 安装对应的 CUDA Toolkit。也可从 NVIDIA 官方 redistributable 清单下载并逐项校验 SHA256，把 NVCC、cudart、CCCL、cuBLAS 的 `bin/include/lib/nvvm` 合并到独立前缀，保留许可证；本轮使用这一方式，不替换显卡驱动。
4. 在独立 MSVC 依赖前缀构建 HDF5 1.14.6，启用 C++ 与 HL。KLU 可使用项目固定的 SuiteSparse FetchContent 源码，用相同 MSVC 构建。不要指向 W0 的 GCC/UCRT64 依赖前缀。
5. OpenMP 使用 MSVC 的 `/openmp:llvm /openmp:experimental`，以编译现有 SIMD 与 atomic 指令。将对应 MSVC 工具集已安装的 `Microsoft.VC143.OpenMP.LLVM` 目录加入本次运行 PATH；本轮探针实际依赖 `libomp140.x86_64.dll`。这份 Visual Studio 运行库标为 nonredistributable，不复制进 ARCH 分发包，使用者需在自己的工具链中提供；参见[微软说明](https://learn.microsoft.com/en-us/cpp/build/reference/openmp-enable-openmp-2-0-support?view=msvc-170)。
6. 若使用稀疏烧核，准备 cuDSS 0.8 的 `include/cudss.h`、`lib/cudss.lib` 和 `bin/cudss64_0.dll`；运行 PATH 同时包含匹配的 cuBLAS 与 cuBLASLt DLL。

正式验证脚本需要原生 Python 3.9+ 的 API，以及 NumPy／h5py；本轮使用 Python 3.14.10、NumPy 2.5.4、h5py 3.16.0。较旧的 Python 3.7 可运行安装辅助脚本，但不能作为功能验收环境。若要生成来源验收证据，源码树必须保留 Git 信息并安装 Git LFS，不能关闭 LFS 过滤器来绕过依赖。

工具前缀、构建目录和 CPU 的既有目录分开。无需把机器路径、编译器环境或个人工具配置提交到仓库。

## 配置与编译

以下示例在 **Developer Command Prompt／CMD** 中执行（换行符为 `^`，不是 PowerShell 反引号）。先将示例路径替换为本机位置，并进入 ARCH 项目根目录：

```bat
call "C:\Tools\VS2022\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -vcvars_ver=14.44
set "ARCH_CMAKE=C:\Tools\cmake"
set "ARCH_CUDA=C:\Tools\cuda-12.9"
set "ARCH_MSVC_DEPS=C:\Tools\arch-msvc-deps"
set "ARCH_CUDSS=C:\Tools\cudss-0.8"
set "PATH=%ARCH_CUDA%\bin;%ARCH_MSVC_DEPS%\bin;%ARCH_CUDSS%\bin;C:\Tools\ninja;%PATH%"
"%ARCH_CMAKE%\bin\cmake.exe" -S . -B build-cuda-win -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe ^
  "-DCMAKE_CUDA_COMPILER=%ARCH_CUDA%/bin/nvcc.exe" ^
  "-DCMAKE_RC_COMPILER=C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe" ^
  "-DCUDAToolkit_ROOT=%ARCH_CUDA%" -DCMAKE_CUDA_ARCHITECTURES=native ^
  "-DCMAKE_PREFIX_PATH=%ARCH_MSVC_DEPS%" "-DHDF5_ROOT=%ARCH_MSVC_DEPS%" ^
  "-DCUDSS_ROOT=%ARCH_CUDSS%" -DARCH_ENABLE_CUDA=ON ^
  -DARCH_CUDA_HEAVY_COMPILE_JOBS=1 -DBUILD_TESTING=ON ^
  "-DPython3_EXECUTABLE=C:/Tools/python/python.exe" ^
  "-DARCH_RUNTIME_OUTPUT_DIRECTORY=%CD%/build-cuda-win/bin"
"%ARCH_CMAKE%\bin\cmake.exe" --build build-cuda-win --target ARCH --parallel 1
```

Windows CUDA 固定使用共享 `cudart`，与现有 `CUDA::cudart` 链接一致；不能再同时混入 `cudart_static`。运行 PATH 需包含 CUDA、cuDSS、同 ABI HDF5 和本机 LLVM OpenMP DLL 目录。

`native` 面向配置时可见的 GPU。本轮实际证据使用 `86-real`；移动到其他显卡时需重新配置合适的代码镜像，不能认为 SM86 二进制自动覆盖全部显卡。初次构建保持重型编译并发为 1，再按可用内存调整。

MSVC Host 保留 `/fp:strict`，NVCC 仍使用 `--fmad=false --ftz=false --prec-div=true --prec-sqrt=true`。Windows 与 Linux 的严格浮点契约相同，不启用 fast-math。UTF-8 manifest 的内容共用：MSVC 交给链接器嵌入，GNU/windres 由既有 RC 嵌入，避免重复 manifest ID。

## 切换后端与路径

`.par` 的 `compute_backend` 决定本次进程使用 `cpu`、`cuda` 或 `auto`；`cuda_device` 选择设备序号。启动前设置，切换时重新启动进程。`ARCH_ENABLE_CUDA=ON` 才会编译 GPU 后端；运行时选 `cpu` 不会把该构建变成 CPU-only 构建。

```ini
compute_backend = cuda
cuda_device = 0
```

从项目根目录运行 `build-cuda-win\bin\ARCH.exe simulation/Sod/Sod.par`。显式 `cuda` 的不支持条件应明确报错；验收时检查实际选择记录，不把自动回退或单纯检测到显卡算作 GPU 计算通过。

优先在 `.par` 中使用项目相对路径与 `/`，例如 `EOS_toolkit/tables/helmholtz/helm_table.dat`。Linux 的 `/home/...`、WSL 的 `/mnt/c/...` 不能直接当成原生 Windows 文件路径；原生 Windows 的 `C:/...` 也不能直接在 Linux 使用。输入文件所在目录与进程工作目录不同，涉及相对路径时按[运行与路径说明](Build.zh-CN.md)核对，不批量改写数据内容。

Studio 的配置、模型与 Plotfile 操作见 [Studio 使用说明](Studio.zh-CN.md)。MSVC/CUDA 可以通过 `--build-dir` 选择已配置的独立 Ninja 树。私有 `ARCH_STUDIO_WINDOWS_TOOLS` 在 CPU 工具字段之外允许 `compilerEnvironment`，只含 `INCLUDE`、`LIB`、`LIBPATH` 的绝对本机目录列表；从已初始化的 Microsoft Developer 环境显式生成，不能通过 HTTP、项目参数或 renderer 设置。Main 与 Host 共用同一验证入口；不会继承 `CL`／`_CL_`／`LINK` 等任意编译器选项。

MSVC/CUDA 完整依赖新鲜度仍为 `unknown`，不会冒用 GNU CPU 的 `observed-build-inputs-v1`；实际 Configure／Build、清单、输入和二进制哈希检查保留。当前真实 MSVC/CUDA Build、初始 Preview／AMR、GPU Run／Restart 与正式 Plotfile 读取均有最终验收。使用这棵 CUDA 树自身的 supervisor／publisher，显式编译环境和简明可信 PATH；不要直接复制无关的完整 Developer PATH 或另一构建的工具配置。

## 在同一 CUDA 构建上使用 Studio

先按[桌面指南](WindowsStudioDesktop.zh-CN.md)准备原生 Node、资产与 Electron；相同的 CMake Studio 目标可用于 `build-cuda-win`，并在该树构建 `arch_windows_process`、`arch_windows_build_publish`。Core 仍可独立运行。

使用已选 MSVC 的 x64 Developer PowerShell；也可在前文已初始化的 Developer CMD 中输入 `powershell -NoProfile`，让新窗口继承 `INCLUDE`、`LIB`、`LIBPATH`。在桌面指南的 `$tools` 对象上调整：

```powershell
$repo = (Resolve-Path .).Path
$build = Join-Path $repo 'build-cuda-win'
$vc = Split-Path (Get-Command cl.exe).Source
$sdk = 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64'
$omp = 'C:/Tools/VS2022/VC/Redist/MSVC/14.44.35112/debug_nonredist/x64/Microsoft.VC143.OpenMP.LLVM'
$tools.cmake = 'C:/Tools/cmake/bin/cmake.exe'
$tools.supervisor = Join-Path $build 'arch_windows_process.exe'
$tools.publisher = Join-Path $build 'arch_windows_build_publish.exe'
$tools.ninja = 'C:/Tools/ninja/ninja.exe'
$tools.rc = Join-Path $sdk 'rc.exe'
$tools.path = "$vc;$sdk;C:/Tools/arch-msvc-deps/bin;C:/Tools/cuda-12.9/bin;C:/Tools/cudss-0.8/bin;$omp;C:/msys64/ucrt64/bin;$env:SystemRoot/System32;$env:SystemRoot"
$tools.compilerEnvironment = @{
  INCLUDE = (($env:INCLUDE -split ';' | Where-Object { $_ }) -join ';')
  LIB = (($env:LIB -split ';' | Where-Object { $_ }) -join ';')
  LIBPATH = (($env:LIBPATH -split ';' | Where-Object { $_ }) -join ';')
}
$env:ARCH_STUDIO_WINDOWS_TOOLS = $tools | ConvertTo-Json -Depth 4 -Compress
& C:/Tools/node/node.exe studio/desktop/launch.mjs --project . --build-dir build-cuda-win --binary build-cuda-win/bin/ARCH.exe --source simulation/Sod/Sod.cpp --case Sod --config simulation/Sod/Sod_beginner.par
```

`$tools.objdump` 与可选的 `$tools.terminal` 沿用桌面设置。替换全部示例路径，包括本机 OpenMP redist 版本；私有 SDK `rc.exe` 必须与 CMake 中的绝对路径一致。将同 ABI 的 MSVC HDF5／CUDA／cuDSS 目录放在无关 DLL 前缀之前。

在这一份已编入 CUDA 的程序中，通过 Config 的 `compute_backend` 选择 `cpu`、`cuda` 或 `auto`，构建目录和 MSVC 依赖保持相同；若另选 GCC CPU 二进制，需同时使用匹配的 DLL／工具上下文。初始化 Preview 仍只检查初始状态，实际执行后端由 Run／Restart 确认。

## 验收状态

Windows 通过142项不同科学 CTest（原矩阵141项及补齐的 t=0 拓扑 oracle 1项）、37个规范后端算例和9个分辨率组、8条 AMR restart 路由及9项比较、真实 Host GPU 流程，以及原完整几何目标的0错误 memcheck。Linux 通过72项 CPU CTest、12项受影响 CUDA／真实 fixture 测试及453项 Studio 测试；Windows Studio95项与链接证据7项通过。两侧便携包实际 Electron44.9.1，各225项生产文件比较一致，最新原生窗口及内置 Host 已就绪。此前 W3 人工对话框／曲线证据单独保留；release 与 Zenodo 仍暂缓。


Preview 标准输入按原始字节保留 CRLF；换行变化也会改变配置 revision。私有 Windows 工具上下文中的 SDK `rc.exe` 应与 `CMAKE_RC_COMPILER` 使用同一绝对路径，保留已有工具与指纹绑定检查。

EOS 表读取保留原宿主格式化提取器的边界：实测 MSVC 对低于 binary64 表示范围的 `1e-500`、`-1e-500`、`+1e-500` 报失败，缓冲 reader 同样失败且不发布零；Linux 提取器对这三项接受有符号零。六个可表示测试值和完整规范表各字段仍按原始位精确比较。此差异不改变 EOS 公式、表数据或科学容差。

26类功能逐项对应后闭合，原模型／EOS／几何／方法组合的科学范围继续适用。固定数组测试在 Host／GPU 上逐位检查448次 IEEE 值复制及元素别名；两个真实 EOS 目标另加强70个失败导数槽的检查，已确认强制重编译和实际执行。所有原阈值保持。

在同一 Developer 终端构建并执行已注册的科学测试：

```bat
"%ARCH_CMAKE%\bin\cmake.exe" --build build-cuda-win --parallel 1
"%ARCH_CMAKE%\bin\ctest.exe" --test-dir build-cuda-win -LE "^tooling$" --output-on-failure
```

Linux 专用 tooling 单独执行；原生链接证据测试覆盖 Windows 解析器。规范算例使用 `tools/validate_backend_results.py` 与 `validation/backend/cases.json`；重启使用 `tools/validate_cuda_amr_restart.py`。二者均选择同一构建的 `ARCH.exe` 与 `arch_cuda_single_level_validation.exe`，输出目录应为新建或空目录；不拆开分辨率组，不修改原阈值。本轮曾出现一次首次加载超过既有短元数据查询时限的拒绝，失败已保留；最终验收使用匹配工具配置，未延长 Core／资源限额，不保证所有冷启动环境均稳定。
