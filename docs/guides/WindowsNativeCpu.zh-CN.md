# Windows 11 原生 CPU CLI

[English](WindowsNativeCpu.md) · [构建指南](Build.zh-CN.md) · [工具链预检 W0](WindowsNativeW0.zh-CN.md)

Studio 平台设施及仍受冻结限制的功能见 [W2 平台服务](WindowsStudioPlatform.zh-CN.md)。

Windows CPU CLI 与 Linux 使用相同的 ARCH 目标、求解器、数学库和 HDF5 格式。
本页使用 GCC/MinGW-w64 **UCRT64**；不提供 MSVC、clang-cl、原生 Studio、CUDA 或 MPI 的资格声明。
普通 CPU CLI 不需要 WSL、Node 或 Python 运行时。测试工具的依赖另行准备。

## 工具与目录

先按 [W0 指南](WindowsNativeW0.zh-CN.md)安装工具并构建同 ABI 依赖。本轮组合是
GCC `16.2.0-6`、CMake `4.4.4-3`、Ninja `1.13.2-1`，以及 HDF5 1.14.6、
HighFive 2.9.0、最小 SuiteSparse/KLU 7.13.0 和 UCRT64 zlib。滚动仓库可能更新，
应记录实际版本后重跑适用检查。

在原生 PowerShell 中准备以下路径；根据实际位置修改 `$repo`。源码、工具链与
构建目录优先选择短 ASCII 路径。Windows 与 Linux/WSL 必须使用不同构建目录，
不能共用 `CMakeCache.txt`、对象文件或另一套 C++／LTO 依赖。

```powershell
$tools = Join-Path $env:USERPROFILE 'Tools'
$ucrt = "$tools/msys64/ucrt64"
$deps = "$tools/arch-w0-deps"
$highfive = "$tools/arch-w0/sources/highfive-2.9.0"
$repo = 'C:/src/ARCH'
$build = "$repo/build-windows-cpu"
$install = "$tools/arch-cpu"
```

安装 Runtime 组件会复制三份 EOS 表。先将相应 Git LFS 指针物化并核验：
`EOS_toolkit/tables/helmholtz/helm_table.dat`、`EOS_toolkit/tables/baryon/eos2.tab`、
`eos4.tab`。可在源码 checkout 中使用：

```powershell
git -C $repo lfs pull --include="EOS_toolkit/tables/helmholtz/helm_table.dat,EOS_toolkit/tables/baryon/eos2.tab,EOS_toolkit/tables/baryon/eos4.tab" --exclude=""
if ($LASTEXITCODE -ne 0) { throw 'Required LFS assets are unavailable' }
```

只有 LFS 文本指针不能算已安装数据。本轮使用已核验的离线副本，没有重新下载
历史模拟数据。Sod 使用 IdealGas，但这不免除 Runtime 安装所复制表的身份检查。

再为原生编译和运行选择本进程的环境：

```powershell
$env:Path = "$deps/bin;$ucrt/bin;$env:SystemRoot\System32;$env:SystemRoot"
$env:ComSpec = Join-Path $env:SystemRoot 'System32\cmd.exe'
$env:PATHEXT = '.COM;.EXE;.BAT;.CMD'
```

不要混入 MSYS `usr/bin`、Anaconda 的 HDF5 DLL 或另一套 libstdc++。
`ComSpec` 保留原生反斜杠拼写，`PATHEXT` 保留 `.EXE` 搜索。

## 配置、编译与安装

下面调用既有 CMake 图；不新建 Windows 求解器或第二套依赖实现。HighFive 使用
W0 已准备的固定源码，KLU 使用已安装前缀，避免在配置时另取一个版本。

```powershell
& "$ucrt/bin/cmake.exe" -S $repo -B $build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_C_COMPILER:FILEPATH=$ucrt/bin/gcc.exe" `
  "-DCMAKE_CXX_COMPILER:FILEPATH=$ucrt/bin/g++.exe" `
  "-DCMAKE_RC_COMPILER:FILEPATH=$ucrt/bin/windres.exe" `
  "-DCMAKE_PREFIX_PATH:PATH=$deps" "-DHDF5_ROOT:PATH=$deps" `
  "-DFETCHCONTENT_SOURCE_DIR_HIGHFIVE:PATH=$highfive" `
  -DARCH_ENABLE_CUDA=OFF -DARCH_ENABLE_OPENMP=ON `
  -DARCH_ENABLE_KLU=ON -DARCH_FETCH_SUITESPARSE=OFF `
  -DARCH_BUILD_STUDIO=OFF -DBUILD_TESTING=OFF `
  "-DARCH_RUNTIME_OUTPUT_DIRECTORY:PATH=$build/bin" `
  "-DCMAKE_INSTALL_PREFIX:PATH=$install"
if ($LASTEXITCODE -ne 0) { throw 'ARCH configuration failed' }

& "$ucrt/bin/cmake.exe" --build $build --target ARCH --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'ARCH build failed' }
& "$ucrt/bin/cmake.exe" --install $build --component Runtime
if ($LASTEXITCODE -ne 0) { throw 'ARCH installation failed' }
```

应用为 `$install/bin/ARCH.exe`，示例输入和表位于 `$install/share/arch`。此安装
不是独立分发包：执行时仍须通过上述 PATH 找到所选 UCRT64 和 HDF5 DLL。没有
打包 Studio、发布安装包或变更 release。严格 FP、OpenMP 和生产 IPO 仍由原
CMake owner 管理，不通过改数学公式或容差适配 Windows。

## 运行与路径

```powershell
Set-Location $repo
& "$install/bin/ARCH.exe" Sod "$install/share/arch/simulation/Sod/Sod_beginner.par"
if ($LASTEXITCODE -ne 0) { throw 'ARCH simulation failed' }
```

相对输出目录相对于启动工作目录。命令行参数与 `.par` 中的 `out_dir`、
`restart_file` 可使用 `C:/work/输出` 或 `C:\work\输出`；不全局替换业务字符串。
应用和 Host C++ 测试工具共用进程级 UTF-8 manifest，参数文件使用 UTF-8，
不修改系统区域设置。本轮验证了本机 NTFS 上安装、参数和输出路径中的中文与空格。
长路径、UNC、网络文件系统及跨宿主路径映射不由这一结果推导。
`/mnt/c/...`、`/home/...` 属于 WSL 路径空间，不能仅换斜杠就变成 Windows 路径。

重启沿用 `restart = true` 与 `restart_file = ...`，其余物理兼容性要求不变。
本轮使用 64 个单元的 8 步 Sod 工程样本，从实际第 1 步 checkpoint 重启到第 8 步。
原比较器对重启及 OpenMP 1／4 线程结果的本次零容差比较均通过；这不是新增的全仓
逐位标准，也不替代 Sod 收敛、全部物理模型或跨平台重启资格。

## 定点回归与已知编译器边界

启用 `BUILD_TESTING=ON` 时，CMake 还需要 Python 解释器。完整项目 Python 工具
使用受支持的 Python 3.10+ 环境；本轮旧 Windows Python 仅用于 stdlib 调度和
进程父测试，不作为科学 Python 栈资格。

构建选定目标后可运行：

```powershell
$python = 'C:/path/to/python.exe'
& "$ucrt/bin/cmake.exe" -S $repo -B $build -DBUILD_TESTING=ON `
  "-DPython3_EXECUTABLE:FILEPATH=$python"
if ($LASTEXITCODE -ne 0) { throw 'Test configuration failed' }
& "$ucrt/bin/cmake.exe" --build $build --parallel 2 --target ARCH `
  arch_platform_files arch_cuda_single_level_validation arch_compensated_sum `
  arch_tabular_strict arch_helm_components arch_reduction_contract `
  arch_plotfile_publication arch_checkpoint_compatibility sparse_klu_regression
if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
& "$ucrt/bin/ctest.exe" --test-dir $build --output-on-failure --no-tests=error `
  -R '^(platform_files|running_image_identity|checkpoint_temporal_comparison|compensated_sum|tabular_strict_math|helm_components|reduction_contract|plotfile_publication|checkpoint_compatibility|sparse_klu_161_equations)$'
if ($LASTEXITCODE -ne 0) { throw 'Focused native checks failed' }
```

目标名中的 `arch_cuda_single_level_validation` 是既有 Host checkpoint 比较器，
不启用 GPU。不能将未构建、跳过或未适用的测试计为通过。新增运行映像测试
在首次取 SHA 前重命名真实程序，并将旧启动路径替换为不同内容，验证普通 ASCII
和中文路径下记录的仍是原映像文件。

本机 UCRT64/GCC 16.2.0 下，Plotfile 测试入口的合法 `std::regex` 在 IPO 模式中
可于任何 Grid 操作前失败；隔离对照和混合对象对照已定位到该测试入口。构建规则
仅在这个平台／编译器版本为该入口追加 `-fno-lto`。生产 ARCH、HDF5Writer、
FileFingerprint 及其余检查继续 IPO，原测试源码、断言、`-O3` 和严格 FP 不变。
更换编译器时须重新核对；本地诊断不代表上游已确认此问题。

Core 的 UUID 格式与校验共享，只有 OS 熵源分支不同。运行程序 SHA 在 Windows
从映射映像的文件名取得只读 handle，读取期间禁止写入／删除共享并复核映射名；
查询、打开或读取失败即报错，不退回 `argv[0]` 或空摘要。普通本机 NTFS 的实际
替换测试支持这一范围，不承诺并发改名时必然成功或抵御任意文件系统／内核行为。

数学库、科学数据 schema、重启门和误差预算保持原约定。CPU CLI 样本不替代全模型科学验收；原生 Studio Host、Preview 与窗口的独立合格证据见 [W2](WindowsStudioPlatform.zh-CN.md)／[W3](WindowsStudioDesktop.zh-CN.md)，日常操作见[共用指南](Studio.zh-CN.md)。GPU／MPI 仍另行验收。Zenodo 和 release 另行决定。
