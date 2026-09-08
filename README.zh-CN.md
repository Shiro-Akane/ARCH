# ARCH：自适应反应流 CUDA 流体力学框架

英文原文：[README.md](README.md)。英文版是唯一规范文本；行为或接口发生变化时应先更新英文版。若中英文内容不一致，以英文版为准。

[![C++20](https://img.shields.io/badge/standard-C%2B%2B20-blue.svg)]()
[![Build](https://img.shields.io/badge/build-CMake-orange.svg)]()
[![CPU backend](https://img.shields.io/badge/backend-CPU-success.svg)]()
[![CUDA](https://img.shields.io/badge/CUDA-supported-success.svg)](docs/CudaBackendStatus.zh-CN.md)
[![ARCH code: MIT](https://img.shields.io/badge/ARCH_code-MIT-yellow.svg)](LICENSE)

ARCH 是一个面向可压缩反应流体力学的块自适应有限体积框架。CPU 与 CUDA 共用同一套数学和物理实现。构建时启用 `ARCH_ENABLE_CUDA=ON`，即可在算例配置中选择 CUDA 后端。

项目主要面向两类使用者：

- 学习如何配置、初始化、推进和检查 CFD 算例的学生；
- 直接在源码树中扩展 Riemann 求解器、状态方程、核反应网络、扩散、重力、AMR 或诊断功能的科研人员。

## 项目状态

CPU 与 CUDA 均支持下表功能。本次发布验证范围已通过数值、应用运行、设备安全、
构建与资源检查。受测配置和最终交付审阅状态统一见 [Validation](validation/README.zh-CN.md)。

| 功能 | 共用行为与后端选择 |
| --- | --- |
| 流体力学 | 一维、二维和三维的笛卡尔、柱坐标及球坐标网格 |
| 动态块 AMR | 守恒细化与粗化、边界数据交换及通量修正。使用 CUDA 时，GPU 计算细化指标并迁移网格数据，CPU 管理网格树。 |
| 状态方程 | 理想气体、Helmholtz 及三维／四维表格 EOS |
| 扩散 | 热扩散、黏性扩散和组分扩散，支持 RKL1/RKL2 时间推进 |
| 重力 | 给定的外部重力场 |
| 核燃烧 | 四个内置网络及 pynucastro 生成网络；内置网络支持 NSE 投影。CUDA 使用具备设备数学接口的版本 4 生成网络。 |
| 线性求解 | 小系统使用 DenseLU；稀疏系统在 CPU 上使用 KLU，在 CUDA 上使用 cuDSS。 |
| 输出与重启 | 两侧使用相同的 HDF5 可视化数据和检查点格式，保存 AMR 层级、燃烧能量及时间步控制器状态。 |

版本 1/2 checkpoint 仍可读取；它们缺少有序物理配置身份和 `ENUC`，版本 1 还缺少时间步控制器状态。版本 3 记录实际启用的燃烧、网络与 NSE 身份，以及 EOS 实际加载的表摘要。版本 4 另外保存原始质量分数，避免从组分密度重建时损失精度。CPU 与 CUDA 使用同一格式，后端选择不作为 restart 兼容字段。

`compute_backend = cpu`、`cuda` 或 `auto` 会在初始化时确定执行后端。
显式选择 CUDA 时，不满足运行条件会直接报错。选择 `auto` 时，如果没有可用
GPU，或当前 CUDA 构建不支持所需功能，而 CPU 支持同一组物理配置，程序会在
初始化前选择 CPU 并报告结果；运行过程中不会自动更换后端。

CPU 与 CUDA 共用策略注册、AMR 指标与迁移公式、几何、EOS、反应网络、
数值求解算法、推进调度和 HDF5 格式。Morton 编码与网格拓扑决策由 CPU 负责，
GPU 调用共用公式完成单元计算和守恒迁移。各后端分别管理内存、计算内核、
数据传输和执行同步，并适配各自的稀疏求解库。

## 已实现功能

- 带 ghost exchange、prolongation/restriction、flux register 和 reflux 的守恒块 AMR；
- Cartesian、cylindrical 和 spherical 网格上的维度感知 1D、2D 和 3D 存储；
- SW、VL、Roe、HLL 和 HLLC 通量策略；
- PCM、MUSCL/PLM 和 PPM 重构，以及 Euler、SSPRK2 或 SSPRK3 时间推进；
- 理想气体、自动识别维数的 3D/4D 表格（包括规范化自由能表）和 Timmes Helmholtz 状态方程；
- 共享外部重力、内置或 pynucastro 生成的核燃烧、DenseLU/可选 CPU KLU 或 CUDA cuDSS 线性求解、NSE 投影和 RKL1/RKL2 超时间步扩散；
- HDF5 plot/checkpoint 文件，包括 AMR 叶节点层次的重启。

## 构建

ARCH 面向 Linux/WSL 风格的 C++ 环境。所需工具和库包括：

- C++20 编译器；
- CPU 构建使用 CMake 3.22 或更高版本，CUDA 的要求见下文；
- Python 3.10 或更高版本，用于默认测试配置及验证工具；
- OpenMP，除非配置时关闭；
- HDF5 C++ 和 HL 库；
- Git，以及配置阶段的网络访问，因为 CMake 会获取 HighFive，并在找不到已安装 KLU 库时获取固定版本的 SuiteSparse；
- Git LFS，用于克隆由 LFS 管理的 EOS `.dat` 或 `.h5` 资源。

在仓库根目录配置和构建：

```bash
git lfs pull
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 1
```

CUDA 后端需要 CMake 3.25.2 或更高版本、NVCC 12.0 或更高版本，以及相应
CUDA 工具链支持的宿主编译器。这些版本提供所需的
[CUDA C++20 语言支持](https://cmake.org/cmake/help/latest/release/3.25.html)；
本次参考构建使用 CMake 3.28 和 CUDA 12.3。通过 `CMAKE_CUDA_ARCHITECTURES`
指定目标 GPU：本机编译可使用 `native`，面向多代设备可指定 `80;86;90`
这样的列表。程序会在选择后端前检查编译产物和驱动是否匹配。
下面的示例使用 Ninja，才能分别限制重型编译任务与总并发数。请先安装 Ninja；
若要更换现有构建的生成器，请使用新的构建目录。

```bash
cmake -S . -B build-cuda -G Ninja -DARCH_ENABLE_CUDA=ON \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=native \
  -DARCH_CUDA_HEAVY_COMPILE_JOBS=2 \
  -DARCH_ENABLE_CUDSS=ON -DCUDSS_ROOT=/path/to/cudss \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-cuda/bin"
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-cuda --target ARCH --parallel 4
```

示例保留默认的 CPU KLU，并启用 CUDA cuDSS，同一构建可以在两个后端运行稀疏燃烧。
上述命令构建可执行文件，不会构建完整验证套件；ARCH 程序本身不需要运行时
Python 环境。cuDSS 是可选依赖：若已能自动发现，可省略 `CUDSS_ROOT`；
不需要 CUDA 稀疏燃烧时可设置 `-DARCH_ENABLE_CUDSS=OFF`，也可以使用安装在
用户目录中的 cuDSS。当前适配层要求 cuDSS 0.8 API，并检查运行库版本。
只有求解库与所选网络／EOS 的执行代码实际链接时，程序才接受该配置；缺少依赖
会明确报错。KLU 用于 CPU，cuDSS 用于 CUDA。

示例中的编译保护工具会保留 1.5 GiB 可用内存，并允许最多新增 256 MiB swap。
启用 `--pressure-guard` 后，还会监测 Linux 内存和 I/O 等待情况，持续高压力时停止本次编译。
该选项需要 Linux 内存和 I/O 的 PSI full 计数器。保护工具还需要可用的 pidfd、
子进程托管支持及可读的 `/proc`，会在启动编译前检查这些能力；当前 WSL2 已提供支持。
需要手动停止时，使用 `Ctrl+C` 或 `SIGTERM`，工具会一并清理本次启动的编译器进程。

手动选择 GCC 时，`CMAKE_C_COMPILER`、`CMAKE_CXX_COMPILER` 和
`CMAKE_CUDA_HOST_COMPILER` 应使用配套版本。Release 构建保留 LTO，本地编译的依赖库也需要使用兼容的编译器版本。

默认可执行文件写入 `bin/ARCH`；上述隔离 CUDA 示例写入 `build-cuda/bin/ARCH`，避免跨构建覆盖。KLU 默认启用；CMake 会使用已安装的库，或获取固定版本的 SuiteSparse v7.13.0。

使用 Ninja 时，`ARCH_CUDA_HEAVY_COMPILE_JOBS` 控制重型编译任务的并发数（默认 `1`），
`--parallel` 控制所有任务的总并发数。对于下述中等配置参考，推荐使用
`ARCH_CUDA_HEAVY_COMPILE_JOBS=2`、`--parallel 4`；这套配置已完成
[核心构建测量](validation/backend/results/cold-core-first-law-20260907/release-909/README.zh-CN.md)。
它是实测参考配置，不是最低硬件要求或通用最优值。可用内存较少时降低并发数，
大内存机器可以结合保护工具的测量结果提高两个上限；数值方法和 Release 优化保持不变。
供贡献者使用的测量与重构过程记录见[开发文档](docs/development/README.md)。

本地编译测量以 WSL2、i7-10700 级别 CPU、16 GB 系统内存及 RTX 3060 Ti
级别的 8 GB 显卡作为中等配置参考。模拟所需内存取决于网格、细化层级、核素
数量和稀疏求解器的工作空间。使用 WSL 时，还需检查实际分配给 Linux 的内存，
并为 Windows 留出余量。不同机器使用相同的数值方法和精度设置。

Release 构建会针对本机 CPU 优化，迁移到不同 CPU 架构时请重新构建。
通过 `CMAKE_CUDA_ARCHITECTURES` 指定运行程序的目标 GPU，并选择支持这些目标的 CUDA 工具链。

## Tabular EOS 与自定义网络

Tabular EOS 参数提供 HDF5 路径，表内元数据负责维数选择：

~~~text
eos_type = tabular
eos_table_path = /path/to/model.h5
~~~

文件内部的 `table_rank` 自动选择 3D 或 4D 策略。新 EOS 表应保存比 Helmholtz
自由能并遵循[本地 HDF5 契约](src/physics/eos/TabularEOS.zh-CN.md)；上游
Shen/LS/HS 或 CompOSE 文件必须使用表族专用转换器转换成该契约。仓库目前不
附带外部 EOS 转换器；真实 Shen 来源表评估见
[validation/eos](validation/eos/README.zh-CN.md)。

本文的网络生成流程使用 pynucastro 2.12.0，Python 环境与完整构建步骤见
[网络验证指南](validation/network/README.zh-CN.md#复现这些记录)。复制并编辑示例生成脚本，
选择唯一的 `NETWORK_ID` 和所需核素，然后运行：

~~~bash
cp examples/network/CustomNetworkRecipe.py MyNetwork.py
python3 tools/network/GenerateNetwork.py MyNetwork.py --check
python3 tools/network/GenerateNetwork.py MyNetwork.py
cmake -S . -B build
cmake --build build --parallel 1
~~~

每个生成网络包位于 `src/physics/network/custom/<id>/`。CMake 注册该目录内
实际存在的 ID，并允许多个 ID 共存；`aprox*`/`iso*` 命名空间保留给内置网络。
生成器会检查并备份已有网络，再执行替换。每次运行选择一个网络包：

~~~text
network_name = custom:<id>
use_burn = true
use_nse = false
linear_solver = Auto
~~~

生成器版本 4 仅为清单中声明 `device_callable_math=true` 的网络包启用 CUDA。
两个后端使用同一个生成数学头文件及清单声明的 Jacobian 结构。对于已识别的
内嵌弱反应率表，各后端分别管理只读数据，共用插值、导数和有符号能量积分。
版本 3 或尚未转换为设备端实现的网络包仍只能在 CPU 上运行。生成网络不支持
Timmes NSE 投影。

线性求解器名称不区分大小写。`Auto` 在 ODE 方程总数不超过 31 时选择 DenseLU，
计数包含核素、温度和可选辅助状态。更大系统在 CPU 上使用 SparseKLU，在 CUDA
上使用 cuDSS，前提是相应求解库与网络／EOS 执行代码已构建。显式 SparseKLU
仅适用于 CPU，显式 cuDSS 仅适用于 CUDA；不兼容的组合会在后端构造前报错，
不会静默替换求解器。cuDSS 是可选依赖，但 CUDA 稀疏燃烧必须链接该库。
独立弱反应轨迹、真实生成网络的应用运行及多规模兼容记录统一见
[网络验证](validation/network/README.zh-CN.md)。完整契约和生成器要求见
[研究与 API 参考](docs/Reference.zh-CN.md)。

## 首次运行

运行小型一维 Sod 教学算例：

```bash
export OMP_NUM_THREADS=4
./bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

成功运行后会打印选定的 EOS、求解器、重构、时间积分器、AMR 分辨率和时间步表，并在 `output/first_sod/` 下写入：

```text
SodBeginner_log.dat
SodBeginner_HLLC_plt_0000.h5
SodBeginner_chk_0000.h5
...
```

`Sod_beginner.par` 使用 64 个网格单元；`Sod.par` 提供 128 单元的标准激波管。爆炸波基准位于 `simulation/Sedov/`。

## 文档路径

[模拟算例指南](docs/guides/SimulationCase.zh-CN.md)提供从首次运行、核心 CFD 参数到新建 `Setup`/`Init` 算例的连续学生学习路径。

参数名、可接受取值、API 签名、输出格式、扩展契约和已知工程妥协统一收录在可搜索的[研究与 API 参考](docs/Reference.zh-CN.md)中。

[文档索引](docs/README.zh-CN.md)按读者和主题归纳学习指南、物理说明、API 参考与法律文件入口。

[CUDA 与 GPU-AMR 指南](docs/CudaBackendStatus.zh-CN.md)介绍支持的功能、后端职责与求解器选择。

[验证索引](validation/README.zh-CN.md)汇总数值结果与发布验收状态。
[AMR 验证页](validation/amr/README.zh-CN.md)介绍网格自适应、守恒、几何与重启检查，
并提供相应的可复现记录。

## 仓库结构

[源码导览](src/README.md)按功能连接各实现模块。各模块 README 介绍职责与主要入口，
[贡献者指南](docs/development/README.md)说明实现归属和审阅流程。

```text
ARCH/
├── README.md                  # 英文入口与首次运行，规范文本
├── README.zh-CN.md            # 中文辅助入口
├── LICENSE                    # ARCH 自有内容的 MIT 许可证
├── THIRD_PARTY_NOTICES.md     # 科学软件来源与第三方条款
├── LICENSES/                  # 保留的第三方许可证文本
├── CMakeLists.txt             # CPU/CUDA 构建与分发目标
├── cmake/                    # 依赖发现与构建时生成的绑定
├── simulation/               # 算例实现与可复用示例输入
├── docs/                     # 指南、参考、物理说明和法律索引
├── validation/               # 唯一 V&V 目录：输入、记录、指标与图像
├── EOS_toolkit/              # 按模型归类的运行时 EOS 表
├── src/
│   ├── core/                 # 参数加载、算例注册、公共门面
│   ├── interface/            # ProblemGenerator 适配器
│   ├── data/                 # 守恒量和算例侧状态类型
│   ├── grid/                 # 坐标和有限体积度量
│   ├── amr/                  # 层次、内存池、交换、通量寄存器
│   ├── driver/               # 运行时 dispatch 和算子顺序
│   ├── cuda/                 # 设备计算核、存储与求解库适配
│   ├── numerics/             # 通量、重构、积分、燃烧、扩散
│   ├── physics/              # EOS、重力、核素、网络、NSE、诊断
│   ├── io/                   # 参数、日志、HDF5 plot/checkpoint IO
│   └── main.cpp
├── build/                    # 生成目录，已忽略
├── bin/                      # 生成目录，已忽略
└── output/                   # 生成目录，已忽略
```

## 当前数值边界

- 燃烧、扩散和流体使用对称组合 `B(dt/2)-D(dt/2)-H(dt)-D(dt/2)-B(dt/2)`；因此即使流体子步选择 SSPRK3，耦合方法最高也只有二阶；
- 粗细 AMR 界面以 MUSCL-MinMod 代替 PPM 的宽模板；
- 在无效或近真空状态下，密度、速度、内能和组分保护可能修改守恒更新；
- 共用构建契约在支持的 GNU/Clang/NVIDIA 工具链上关闭 fast-math 与浮点收缩。Release 仍使用 `-march=native`；不保证跨机器逐位一致；
- ARCH 当前提供源码级扩展接口，而不是已安装的公共库 ABI。

收敛和生产研究应遵循 Reference。正式验证结论必须包含可复现输入、参考解、范数和容差。

## 许可证

ARCH 自有内容采用 [MIT License](LICENSE)。第三方派生科学代码和数据保留其上游来源与条款，详见[第三方来源与说明](THIRD_PARTY_NOTICES.zh-CN.md)。MIT 许可证尤其不会重新许可 Timmes 派生的反应网络、NSE 实现、Helmholtz EOS 或表数据。 可选 KLU 后端的 SuiteSparse LGPL/BSD 条款保留在 [LICENSES](LICENSES/) 与[第三方说明](THIRD_PARTY_NOTICES.zh-CN.md)中。
