# Windows 11 原生工具链预检（W0）

[English](WindowsNativeW0.md) · [构建指南](Build.zh-CN.md)

后续 CPU CLI 构建与运行见 [Windows 原生 CPU CLI](WindowsNativeCpu.zh-CN.md)。
以下 W0 结果保留当时探针的配置与适用范围。

本页只负责原生 CPU 工具链、依赖 ABI 与选定数学契约的准备和预检。应用构建、安装、运行与重启已由 [W1 指南](WindowsNativeCpu.zh-CN.md)覆盖，完整 Studio 已由 [W3 指南](WindowsStudioDesktop.zh-CN.md)覆盖；两侧日常操作见共用[使用指南](Studio.zh-CN.md)。下方 W0 观察保留当时范围，不改变数学库、数据 schema 或容差；CUDA／MPI 另行验收。

## 已核对的组合

2026-10-24 的 Windows 11 x64 W0 环境使用以下组合。MSYS2 是滚动发行版，后续
安装可能取得不同版本；须记录实际版本并重跑适用检查，不把本表当作永久锁定。

| 部分 | 版本／选择 |
| --- | --- |
| GCC C/C++、libgcc、libstdc++、libgomp | MSYS2 UCRT64 `16.2.0-6` |
| CMake／Ninja | `4.4.4-3`／`1.13.2-1` |
| zlib | UCRT64 `1.3.2-3`，供 HDF5 压缩使用 |
| HDF5 | 同一 GCC 从上游 `hdf5_1.14.6` 构建，共享 C／CXX／HL，zlib 开启 |
| HighFive | 项目既有 `v2.9.0`，不更换头文件版本 |
| SuiteSparse | 项目 fallback 使用的 `v7.13.0`，只构建 KLU、AMD、COLAMD 和所需 BTF／config |
| 并行 | GCC libgomp；最小 SuiteSparse 构建不引入额外 OpenMP／BLAS 运行库 |

软件仓库当前的 HDF5／SuiteSparse 不一定等于这些版本。本配方采用独立依赖前缀；
不能把 Anaconda／MSVC 的 HDF5 C++ DLL 或另一套 C++／LTO 对象混入此 GNU 图。
已安装 KLU 的发现契约没有强制固定版本；这里选择 7.13.0 是为了与既有 fallback
配方一致，不是新设全局版本拒绝规则。

## 安装工具

按 [MSYS2 官方安装说明](https://www.msys2.org/docs/installer/)下载并核验 bootstrap
摘要／签名。本轮使用 `msys2-base-x86_64-20260927.sfx.exe`，安装到
`%USERPROFILE%\Tools\msys64`；不修改系统 PATH、默认编译器或既有 Anaconda。
选择短、ASCII、无空格的本机 NTFS 安装位置。

自解压程序属于 GUI executable；在 PowerShell 中等待真实退出码：

```powershell
$tools = Join-Path $env:USERPROFILE 'Tools'
$p = Start-Process .\msys2-base-x86_64-20260927.sfx.exe `
  -ArgumentList '-y', "-o$tools" -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "MSYS2 extraction failed: $($p.ExitCode)" }
& "$tools\msys64\ucrt64.exe"
```

在 UCRT64 shell 内完整更新，然后安装原生包：

```bash
pacman -Syu
# 若更新关闭了终端，重新打开 UCRT64 shell 后继续完整更新。
pacman -Syu --needed mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-libgomp mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-zlib
pacman -Q
```

包管理可以使用 MSYS shell；后面的编译和执行使用原生 PowerShell。将工具安装在
Windows 不意味着需要改变现有 Linux／WSL 环境。

## 选择本进程的工具与依赖前缀

以下 PowerShell 示例使用本轮布局。`$sources` 存放固定上游源码，`$deps` 只接收
该原生工具链构建的依赖。更换编译器／ABI 时使用新的构建和依赖目录。

```powershell
$tools = Join-Path $env:USERPROFILE 'Tools'
$ucrt = "$tools\msys64\ucrt64"
$work = "$tools\arch-w0"
$sources = "$work\sources"
$deps = "$tools\arch-w0-deps"
$env:Path = "$deps\bin;$ucrt\bin;$env:SystemRoot\System32;$env:SystemRoot"
$env:ComSpec = Join-Path $env:SystemRoot 'System32\cmd.exe'
$env:PATHEXT = '.COM;.EXE;.BAT;.CMD'
& "$ucrt\bin\g++.exe" -v
& "$ucrt\bin\cmake.exe" --version
& "$ucrt\bin\ninja.exe" --version
$common = @('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
  "-DCMAKE_C_COMPILER=$ucrt\bin\gcc.exe",
  "-DCMAKE_CXX_COMPILER=$ucrt\bin\g++.exe",
  "-DCMAKE_MAKE_PROGRAM=$ucrt\bin\ninja.exe",
  "-DCMAKE_INSTALL_PREFIX=$deps", '-DCMAKE_CXX_STANDARD=20',
  '-DCMAKE_POLICY_VERSION_MINIMUM=3.5')
```

`ComSpec` 使用原生反斜杠拼写：本轮复现了将其设为 `C:/Windows/.../cmd.exe`
时，CMake 的命令链在进入 GCC 前报语法错误。普通文件路径能使用 `/` 和 `\`，
不代表每个命令解释器都接受相同的自身启动字符串。传给 CMake 的自定义路径缓存
应声明为 `:PATH`／`:FILEPATH` 并使用规范化路径；不要将未类型化的 `C:\Users...`
字符串直接写进生成的 CMake 脚本，也不要全局替换业务字符串中的斜杠。
从 WSL 启动 Windows 工具时还要保留原生 `PATHEXT`；缺少 `.EXE` 搜索会使 windres
调用无扩展名 `gcc` 的预处理步骤失败，即使直接执行 `gcc.exe` 已成功。

## 从固定源码构建依赖

从官方标签准备以下源码目录，并记录 archive／commit 身份：

- [HDF5 `hdf5_1.14.6`](https://github.com/HDFGroup/hdf5/releases/tag/hdf5_1.14.6) → `$sources\hdf5-1.14.6`。
- [HighFive `v2.9.0`](https://github.com/BlueBrain/HighFive/releases/tag/v2.9.0) → `$sources\highfive-2.9.0`。
- [SuiteSparse `v7.13.0`](https://github.com/DrTimothyAldenDavis/SuiteSparse/releases/tag/v7.13.0) → `$sources\suitesparse-7.13.0`。

若只展开 SuiteSparse 子集，保留顶层 CMake 以及 `SuiteSparse_config / cmake_modules /
KLU / AMD / COLAMD / BTF`。不修改上游源码或项目的依赖声明。逐条检查命令退出码；
失败后停止，保留日志并修正后再继续，不能用旧缓存／旧 DLL 当作新构建成功。

```powershell
& "$ucrt\bin\cmake.exe" -S "$sources\hdf5-1.14.6" -B "$work\build-hdf5" @common `
  -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF -DBUILD_TESTING=OFF `
  -DHDF5_BUILD_CPP_LIB=ON -DHDF5_BUILD_HL_LIB=ON -DHDF5_BUILD_FORTRAN=OFF `
  -DHDF5_BUILD_TOOLS=OFF -DHDF5_BUILD_EXAMPLES=OFF -DHDF5_ENABLE_PARALLEL=OFF `
  -DHDF5_ENABLE_Z_LIB_SUPPORT=ON -DHDF5_ENABLE_SZIP_SUPPORT=OFF `
  -DHDF5_ENABLE_NONSTANDARD_FEATURE_FLOAT16=OFF "-DZLIB_ROOT:PATH=$ucrt"
& "$ucrt\bin\cmake.exe" --build "$work\build-hdf5" --parallel 2
& "$ucrt\bin\cmake.exe" --install "$work\build-hdf5"

& "$ucrt\bin\cmake.exe" -S "$sources\suitesparse-7.13.0" -B "$work\build-suitesparse" @common `
  '-DSUITESPARSE_ENABLE_PROJECTS=klu;amd;colamd' -DKLU_USE_CHOLMOD=OFF `
  -DSUITESPARSE_USE_CUDA=OFF -DSUITESPARSE_USE_FORTRAN=OFF -DSUITESPARSE_REQUIRE_BLAS=OFF `
  -DBLA_VENDOR=Generic -DSUITESPARSE_USE_OPENMP=OFF -DSUITESPARSE_USE_PYTHON=OFF `
  -DSUITESPARSE_DEMOS=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON
& "$ucrt\bin\cmake.exe" --build "$work\build-suitesparse" --parallel 2
& "$ucrt\bin\cmake.exe" --install "$work\build-suitesparse"

& "$ucrt\bin\cmake.exe" -S "$sources\highfive-2.9.0" -B "$work\build-highfive" @common `
  "-DHDF5_ROOT:PATH=$deps" -DHIGHFIVE_UNIT_TESTS=OFF -DHIGHFIVE_EXAMPLES=OFF `
  -DHIGHFIVE_BUILD_DOCS=OFF -DHIGHFIVE_USE_BOOST=OFF -DHIGHFIVE_USE_EIGEN=OFF `
  -DHIGHFIVE_USE_XTENSOR=OFF -DHIGHFIVE_PARALLEL_HDF5=OFF
& "$ucrt\bin\cmake.exe" --install "$work\build-highfive"
```

HDF5 的 `_Float16` 扩展在本组合中会因 `FLT16_MAX` 缺失而编译失败；使用其官方
开关关闭该未使用扩展，保留 ARCH 所需的 binary64／native-long-double 语义。
这不是降低数学精度，也不改变数据 schema。`BLA_VENDOR=Generic` 沿用项目的
最小 KLU 配方；HighFive 2.9 的 CMake 最低版本声明为 3.1，CMake 4 下用
`CMAKE_POLICY_VERSION_MINIMUM=3.5` 完成外部依赖配置，不补丁其源码。这些事实不
替代后续对顶层 ARCH 构建／FetchContent 的原生适配验收。

## 定点复验与验收边界

原生检查必须实际执行 Windows PE 程序，记录 C/C++ 版本、目标三元组、CRT、
实际 DLL 路径与哈希。`x86_64-w64-mingw32` 三元组本身不能区分 UCRT／MSVCRT。
包来源、编译／链接和运行证据共同确定 ABI，不能只看 `.exe` 后缀或目录名。

本轮 W0 需要保留严格 FP（无 fast-math、无隐式 FMA 合并）、原 C/C++ IPO 链接
探针，以及以下证据：

- `long double` 的有效尾数和 `max_digits10`，运行时大于 binary64 精度的增量与
  原生文本精确往返；同时检查次正规数、负零、非有限判断和显式 FMA 残差。
- 原 `test_compensated_sum.cpp`、HelmComponentsRegression、严格 Tabular 测试和
  SparseKLURegression；Helm 使用匹配标签的真实表，不使用 LFS 指针。
- 原归约测试的 serial／OpenMP 1 与 4 线程配置；另由独立诊断观察实际线程团队
  和加载的 libgomp 路径，不将它冒充原归约测试的逐进程模块采样。
- HDF5 CXX／HL 与 HighFive 2.9 的创建、关闭、重开、标量／数组／attribute 读回，
  以及目标文件系统上的路径行为。小样本不替代生产 Plotfile／checkpoint 验收。

下例复验原有补偿求和测试；`$repo` 指向冻结源码的本机 NTFS 副本，产物放在工作目录：

```powershell
$repo = 'C:\path\to\ARCH'
$flags = @('-std=c++20', '-O3', '-march=native', '-DNDEBUG',
  '-fno-fast-math', '-fno-math-errno', '-ffp-contract=off')
& "$ucrt\bin\g++.exe" @flags "-I$repo\src" "-I$repo\tests" `
  "$repo\tests\host\numerics\test_compensated_sum.cpp" -o "$work\compensated-sum.exe"
& "$work\compensated-sum.exe"
```

冻结的数学／测试和 Studio 读写实现不为编译器改变。已有逐位要求仅在其原适用
测试中保持；不增加全仓 ULP 门，也不因换平台放宽容差。LLP64 的 `long` 宽度与
Linux LP64 不同，必须记录并在受影响消费者中核查。长路径、UNC、文件身份竞争与
完整 Studio 启动仍需各自证据，不能由普通文件读回推导。

## 本轮结果与 UTF-8 路径条件

在上述候选组合中，原生 C／C++ IPO 检查通过；`long double` 尾数为 64 位，
`max_digits10=21`，运行时扩展精度与文本往返通过。原补偿求和、Helm、严格
Tabular、KLU 161 方程、串行及 OpenMP 1／4 线程归约测试通过，实际 OpenMP
团队为 4。KLU 最大绝对误差为 `2.22045e-16`，仍使用原测试的 `1e-11` 阈值。
HDF5 CXX／HL／HighFive 2.9 小样本的压缩与逐位读回通过。它们是定点 W0 证据，
不代表完整 ARCH／Studio 已完成 Windows 构建和运行。

首组 11 个用例中 10 个通过；默认进程代码页 936 下，UTF-8 窄字符串无法打开
同一中文路径，路径用例失败。仅给另外三个探针加入进程级 UTF-8 manifest 后，
`GetACP()` 为 65001，普通文件、原 Helm 加载器及 HDF5／HighFive 在包含中文和
空格的路径上均通过。保留首组失败；不能将两个配置合写为默认配置全绿。

使用 [Windows 官方的进程级 UTF-8 设置](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page)，
无需改变系统区域设置。探针使用的 `utf8.manifest` 内容为：

```xml
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly manifestVersion="1.0" xmlns="urn:schemas-microsoft-com:asm.v1">
  <assemblyIdentity type="win32" name="ARCH.W0.NativeUtf8Probe" version="1.0.0.0"/>
  <application>
    <windowsSettings>
      <activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>
    </windowsSettings>
  </application>
</assembly>
```

在相邻的 `utf8.rc` 中声明 `1 24 "utf8.manifest"`，由同一 UCRT64 的 `windres.exe`
编译资源并链接到探针；CMake 中启用 `RC` 并将该 `.rc` 加入相应 target sources。
本轮只在独立探针使用这一配置，没有写入生产 CMake 或改变冻结库。后续 W1
应在应用入口统一约定 UTF-8 并验证跨进程传递；manifest 不会自动纠正混合编码
字符串，也不等于 UNC、长路径或所有 Win32 API 已完成验收。
