# ARCH：自适应反应流 CUDA 流体力学框架

英文原文：[README.md](README.md)。

[![C++20](https://img.shields.io/badge/standard-C%2B%2B20-blue.svg)]()
[![Build](https://img.shields.io/badge/build-CMake-orange.svg)]()
[![CPU CI](https://github.com/Shiro-Akane/ARCH/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Shiro-Akane/ARCH/actions/workflows/ci.yml)
[![CPU backend](https://img.shields.io/badge/backend-CPU-success.svg)]()
[![CUDA](https://img.shields.io/badge/CUDA-supported-success.svg)](docs/CudaBackendStatus.zh-CN.md)
[![ARCH code: MIT](https://img.shields.io/badge/ARCH_code-MIT-yellow.svg)](LICENSE)

ARCH 是一个用于模拟可压缩流体运动、传热与反应的计算框架。它既适合初学者学习计算流体力学（CFD），也方便研究人员直接在源码中扩展底层的物理方程和数值方法。

ARCH 采用有限体积法：将流体区域划分为网格单元，并追踪它们之间质量、动量和能量的交换。为了高效捕捉细节，自适应网格细化（AMR）技术只在需要的地方动态插入更小的单元，从而避免了全局使用极细网格带来的高昂计算成本。CPU 与 CUDA 执行共用同一套数学与物理核心，各自的后端仅负责调度计算任务和管理数据存储。

想要运行你的第一个模拟，请遵循下方的[构建](#构建)与[首次运行](#首次运行)说明。之后，[模拟算例指南](docs/guides/SimulationCase.zh-CN.md)将带你了解如何读取输出结果、修改参数以及创建自己的仿真场景。请放心，初学者示例既不需要 GPU，也不需要配置任何核反应网络。

## 项目状态

CPU 与 CUDA 后端均完整支持以下功能。当前的发布版本已顺利通过严格的数值精度、应用运行、设备安全、构建和资源检查。详细的测试配置与最终的交付审阅状态，均记录在 [Validation](validation/README.zh-CN.md) 验证套件中。

[持续集成](tests/README.zh-CN.md#github-持续集成)检查新改动的工具行为、CPU 构建和
回归结果；GPU 与独立科学验证的结果另见 Validation。

| 功能 | 共用行为与后端选择 |
| --- | --- |
| 流体力学 | 一维、二维和三维的笛卡尔、柱坐标及球坐标网格 |
| 动态块 AMR | 守恒细化与粗化、边界数据交换及通量修正。使用 CUDA 时，GPU 计算细化指标并迁移网格数据，CPU 管理网格树。 |
| 状态方程（EOS） | 描述密度、温度、压力与能量之间的关系，支持理想气体、Helmholtz 及三维／四维表格 |
| 扩散 | 热扩散、黏性扩散和组分扩散，支持 RKL1/RKL2 时间推进 |
| 重力 | 给定的外部重力场 |
| 核燃烧 | 四个内置网络及 pynucastro 生成网络；内置网络还支持核统计平衡（NSE），可根据平衡条件确定组分。 |
| 线性求解 | 小系统使用 DenseLU；稀疏系统在 CPU 上使用 KLU，在 CUDA 上使用 cuDSS。 |
| 输出与重启 | 两侧使用相同的 HDF5 可视化数据和检查点格式，保存 AMR 层级、燃烧能量及时间步控制器状态。 |

你可以在参数文件中设置 `compute_backend = cpu`、`cuda` 或 `auto` 来选择模拟运行的硬件。如果明确请求 `cuda` 但构建或硬件不支持，程序将报错。若设为 `auto`，当 CUDA 不可用且 CPU 支持所需功能时，ARCH 会在启动时平滑回退到 CPU。一旦模拟开始，后端将保持固定。[CUDA 指南](docs/CudaBackendStatus.zh-CN.md)详细说明了这些选项以及 CPU 和 GPU 在 AMR 过程中的协作方式。

检查点（Checkpoint）保存了无缝恢复模拟所需的所有状态信息（包括网格和流体组分）。由于 CPU 和 CUDA 后端使用完全相同的文件格式，你可以自由地在不同的后端之间进行重启。[参考手册](docs/Reference.zh-CN.md)详细列出了保存的字段，以及恢复模拟时必须保持一致的物理设置。

较小的 AMR 工作负载可以先使用 CPU，再通过代表性算例判断 CUDA 是否更快。
[后端性能指南](docs/CudaBackendStatus.zh-CN.md#按性能选择后端)说明了已测的 CPU/CUDA
对照结果及其适用范围。

## 已实现功能

ARCH 提供 SW 和 VL 通量矢量分裂，以及 Roe、HLL 和 HLLC 黎曼求解器，用于估计跨越单元边界的输运。在单元面的空间重构方面，支持 PCM、MUSCL/PLM 和 PPM。时间积分由 Euler、SSPRK2 或 SSPRK3 方案处理，而扩散过程则采用 RKL1 或 RKL2 超时间步方法。所有这些数值方法的选择都完全独立于 CPU/CUDA 后端。

内置的教学算例已经预先配置了合适的方法，你可以放心从这些设置开始。[算例指南](docs/guides/SimulationCase.zh-CN.md)会在深入各个参数前，先解释每种方法的作用。如需查看所有允许的方法组合，请参阅[参考手册](docs/Reference.zh-CN.md)。

## 构建

ARCH 必须在 Linux 环境中编译；Windows 用户请使用 WSL2 Linux 终端。你可以在下方选择构建纯 CPU 版本或 CPU/CUDA 双支持版本。由于 CUDA 可执行程序同时也支持在 CPU 上运行，因此不需要将两者都编译一遍。

第一次运行建议从 `cpu-release` 开始，需要 GPU 执行时再选择 `cuda-release`。
**启用 `ARCH_ENABLE_CUDA=ON` 会显著拉长编译时间**：除了 CPU 应用，还需要 NVCC
编译设备代码，增加模板实例化与链接工作。编译压力主要落在**主机内存，而非显存**。
较大的生成网络、更多目标 GPU 架构会进一步增加工作量。在 `.par` 中设置
`compute_backend = cpu` 只选择运行后端，不会消除已启用 CUDA 的构建成本。

### 获取源码

推荐用户拉取 `main` 分支：

```bash
git clone --branch main --single-branch https://github.com/Shiro-Akane/ARCH.git
cd ARCH
```

更新已有的 `main` 工作目录时，先保存自己的修改，再在仓库目录内运行
`git pull --ff-only`。如果不能直接更新，Git 会停止，不会重置你的工作。

### 准备工具

先在 Linux 环境中安装以下工具及开发库：

- 支持 C++20 的编译器、CMake 3.22 或更高版本、Ninja 和 Git。
- HDF5 的 C++ 与高层接口库，以及用于 CPU 并行计算的 OpenMP。
- 使用 CUDA 时，还需要 CMake 3.25.2 或更高版本、CUDA Toolkit 12.0 或更高版本、
  该工具链支持的宿主编译器及可用的 NVIDIA 驱动。下方带内存监控的编译命令还
  需要 Python 3.10 或更高版本。

使用 WSL2 时，NVIDIA 驱动安装在 **Windows**，CUDA Toolkit 安装在 WSL 内；
不要在 WSL 内安装 Linux 显示驱动。安装步骤见
[NVIDIA 的 WSL 指南](https://docs.nvidia.com/cuda/wsl-user-guide/index.html)。

CMake 会在配置时下载 HighFive。CPU 稀疏求解器 KLU 默认启用：程序优先使用
已安装的库，否则下载固定版本的 SuiteSparse v7.13.0。因此，配置阶段需要联网。

下方命令都在仓库根目录执行。`cmake --preset ...` 使用项目保存的配置检查依赖
并准备构建目录，`cmake --build ...` 才开始编译。预设使用 Ninja 执行编译任务，
不需要再运行 `make`。如果同名构建目录已配置过其他编译器或构建工具，请换一个空目录。

配置时会保留 ARCH 的依赖摘要，并收起随源码构建的依赖库反复打印的参数。
Debug 和 Release 都会保留警告与错误；需要完整配置或编译命令时，参见
[构建输出](docs/guides/Build.zh-CN.md#构建输出)。

### 方案 A：使用 CPU

```bash
cmake --preset cpu-release
cmake --build build-cpu --target ARCH --parallel 1
```

这会将可执行程序生成在 **`build-cpu/bin/ARCH`**。现在你可以跳至[首次运行](#首次运行)部分。
两个 Release 预设均设置 `BUILD_TESTING=OFF`，不影响模拟功能。
改为 `ON` 会注册额外测试目标；构建默认目标集合时，会编译更多可执行程序，增加
耗时与主机内存压力，CUDA 测试尤其明显。`--target ARCH` 只构建应用及其依赖，
不会构建独立测试套件。需要[自验证](tests/README.zh-CN.md)时再启用测试即可。
`--parallel 1` 标志将编译任务限制为单线程，以节省内存。

### 方案 B：同时支持 CPU 与 CUDA

请在将要运行 ARCH 的 GPU 所在机器上执行。预设通过
`CMAKE_CUDA_ARCHITECTURES=native` 为本机 GPU 编译，启用 `ARCH_ENABLE_CUDA=ON`，
并将重型编译任务限制为一个。

```bash
cmake --preset cuda-release
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-cuda --target ARCH --parallel 1
```

编译完成后，程序位于 **`build-cuda/bin/ARCH`**。外层 Python 工具负责监测内存
和磁盘压力，实际编译仍由 CMake 启动。按这里的设置，工具会在可用内存低于
1.5 GiB、swap 新增超过 256 MiB，或者内存与 I/O 中任一等待指标持续超限时停止编译。
它不改变编译出的数值算法。

如果需要在 CUDA 上进行**稀疏核燃烧求解**，请在配置前另行安装 cuDSS 0.8。
CMake 会自动查找它；如果安装在自定义目录，可在配置命令中追加
`-DCUDSS_ROOT=/your/installed/cudss`，并将路径替换为实际安装位置。
未安装 cuDSS 时，程序可以使用其他 CUDA 功能及稠密燃烧求解，但会拒绝 CUDA
稀疏燃烧请求。KLU 负责 CPU 稀疏求解，不能在 CUDA 上代替 cuDSS。

### 加快编译与获取可选数据

上述命令从单任务编译开始。希望加快编译时，可按[构建指南](docs/guides/Build.zh-CN.md)
调整并行数并监测内存。指南中的中等配置实测参考为 WSL2、i7-10700、16 GB
系统内存和 RTX 3060 Ti 8 GB 显卡，采用两个重型编译任务、四个总任务。
指南也说明了如何选择编译器、为其他 GPU 构建，以及运行测试套件。
这些设置保留 Release 优化。

首次运行的 Sod 算例不需要 EOS 表。使用 Helmholtz 或其他由 LFS 管理的表数据时，
安装 Git LFS，并在仓库根目录执行 `git lfs pull` 即可。ARCH 运行模拟本身不需要 Python。

拉取源码后，可按[测试指南](tests/README.zh-CN.md)先运行无需 GPU 的工具检查，再编译
CPU 或 CUDA 测试，并执行对应配置的完整程序与重启检查。测试源码和小型参考数据
均随仓库提供，测试程序在本机编译。

## 首次运行

Sod 激波管在初始时刻包含两侧密度和压力不同的气体。撤去两者之间的假想隔板
后，会出现激波、接触面和膨胀波。这个小型一维算例适合用来确认程序能够运行，
并熟悉如何查看输出。

完成方案 A 的编译后，在仓库根目录执行：

```bash
export OMP_NUM_THREADS=4
./build-cpu/bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

其中，`Sod` 选择算例定义，`.par` 文件提供参数，`OMP_NUM_THREADS` 控制 CPU
工作线程数，不影响数值精度设置。如果使用方案 B，可执行文件
路径应换成 `./build-cuda/bin/ARCH`。需要 GPU 执行时，先复制示例参数文件，
在副本中加入 `compute_backend = cuda`，再运行该副本。

成功运行后，程序会打印所选方法和时间步表，并在 `output/first_sod/` 下写入：

```text
SodBeginner_log.dat
SodBeginner_HLLC_plt_0000.h5
SodBeginner_chk_0000.h5
...
```

日志是可以直接阅读的文本；名称包含 `_plt_` 的文件保存供查看的流体场，
`_chk_` 文件则是用于重启的检查点。HDF5 是这些二进制数据文件采用的格式。
[算例指南](docs/guides/SimulationCase.zh-CN.md)会介绍如何读取场数据并比较结果。
这个示例使用 64 个单元；`simulation/Sod/Sod.par` 提供 128 单元的标准激波管，
`simulation/Sedov/` 则提供爆炸波示例。

## Tabular EOS 与自定义网络

当理想气体或内置反应网络不足以描述目标问题时，可以使用这些扩展。首次运行
不需要配置它们。

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

使用 CUDA 时，通过随附生成器创建具备设备端数学能力的网络包。清单会声明
这项能力，CMake 在注册 CUDA 执行组合前检查所需的包接口。两个后端使用同一个
生成数学头文件及清单声明的 Jacobian 结构。对于已识别的内嵌弱反应率表，各后端
分别管理只读数据，共用插值、导数和有符号能量积分。仅提供 CPU 接口的网络包
仍可在 CPU 上运行。生成网络不支持 Timmes NSE 投影。

线性求解器名称不区分大小写。`Auto` 在 ODE 方程总数不超过 31 时选择 DenseLU，
计数包含核素、温度和可选辅助状态。更大系统在 CPU 上使用 SparseKLU，在 CUDA
上使用 cuDSS，前提是相应求解库与网络／EOS 执行代码已构建。显式 SparseKLU
仅适用于 CPU，显式 cuDSS 仅适用于 CUDA；不兼容的组合会在后端构造前报错，
不会静默替换求解器。cuDSS 是可选依赖，但 CUDA 稀疏燃烧必须链接该库。
独立弱反应轨迹、真实生成网络的应用运行及不同网络规模的检查记录统一见
[网络验证](validation/network/README.zh-CN.md)。完整契约和生成器要求见
[研究与 API 参考](docs/Reference.zh-CN.md)。

## 文档路径

[模拟算例指南](docs/guides/SimulationCase.zh-CN.md)提供从首次运行、核心 CFD 参数到新建 `Setup`/`Init` 算例的连续学生学习路径。

参数名、可接受取值、API 签名、输出格式和扩展要求统一收录在可搜索的
[研究与 API 参考](docs/Reference.zh-CN.md)中。

[文档索引](docs/README.zh-CN.md)按读者和主题归纳学习指南、物理说明、API 参考与法律文件入口。

[CUDA 与 GPU-AMR 指南](docs/CudaBackendStatus.zh-CN.md)介绍支持的功能、后端职责与求解器选择。

[验证索引](validation/README.zh-CN.md)解释测试了什么，以及如何理解结果。
各模块页面先介绍科学检查，再链接详细报告、日志和实测硬件配置。这些记录供
复现与审阅使用，不是首次运行前必须完成的额外配置步骤。修改源码的开发者还应
阅读[贡献者指南](docs/development/README.md)。

发现疑似安全漏洞时，请先按[安全报告指南](SECURITY.zh-CN.md)联系维护者，不要直接公开
漏洞细节。普通构建问题和数值差异可以提交到 [GitHub Issues](https://github.com/Shiro-Akane/ARCH/issues)。

## 仓库结构

[源码导览](src/README.md)按功能连接各实现模块。各模块 README 介绍职责与主要入口，
[贡献者指南](docs/development/README.md)说明实现归属和审阅流程。
[构建模块指南](cmake/README.md)解释 CMake 如何组合应用、可选后端和测试组。

```text
ARCH/
├── README.md                 # 英文入口与首次运行，规范文本
├── README.zh-CN.md           # 中文辅助入口
├── SECURITY.md               # 安全问题报告说明
├── SECURITY.zh-CN.md         # 中文安全报告指南
├── .gitleaks.toml            # 共用凭据扫描规则
├── .github/                  # 审阅归属、维护说明与持续集成
│   ├── CODEOWNERS            # 默认代码审阅负责人
│   ├── MAINTENANCE.md        # 维护职责与 CI 设置入口
│   └── workflows/            # CPU／工具工作流及说明
├── LICENSE                   # ARCH 自有内容的 MIT 许可证
├── THIRD_PARTY_NOTICES.md    # 科学软件来源与第三方条款
├── LICENSES/                 # 保留的第三方许可证文本
├── CMakeLists.txt            # 构建顺序与模块启用条件
├── CMakePresets.json         # CPU/CUDA 应用及开发预设
├── cmake/                    # 构建模块与 CUDA 绑定辅助工具
│   ├── BuildOptions.cmake    # 用户选项、编译器和优化策略
│   ├── Application.cmake     # 应用与共用数值目标
│   ├── CustomNetworks.cmake  # 生成包契约及注册
│   ├── CudaBackend.cmake     # CUDA/cuDSS 发现与后端目标
│   ├── Dependencies.cmake    # OpenMP、HDF5、HighFive 与 KLU
│   ├── tests/
│   │   ├── HostTests.cmake   # 宿主与 IO 回归目标
│   │   └── CudaTests.cmake   # CUDA 回归目标
│   └── templates/            # 生成的轻量绑定，不复制物理实现
├── simulation/               # 算例实现与可复用示例输入
├── docs/                     # 指南、参考、物理说明和法律索引
├── validation/               # 唯一 V&V 目录：输入、记录、指标与图像
├── tests/                    # 本地编译的检查与小型参考
│   ├── host/                 # 宿主契约与共用接口
│   ├── cuda/                 # 设备执行与 CPU/CUDA 一致性
│   ├── math/                 # 共用数值检查
│   ├── fixtures/             # 独立参考与受控输入
│   ├── tooling/              # 验证和构建工具的 Python 测试
│   └── smoke/                # 短时完整程序检查
├── tools/                    # 验证、源码审查与资源保护
│   └── network/              # pynucastro 包生成
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

选择更高阶的流体积分器，并不能自动提升所有耦合物理过程的精度。燃烧、扩散和流体运动采用对称的算子分裂顺序进行积分：`B(dt/2)-D(dt/2)-H(dt)-D(dt/2)-B(dt/2)`，其中每个字母代表将该过程推进指定的时间跨度。由于这种耦合机制，组合方法的最高精度被限制为二阶，即使流体子步本身使用的是 SSPRK3。请注意，这一限制并不适用于纯流体计算。此外，在 AMR 粗细网格的交界处，程序会安全地回退使用 MUSCL-MinMod 重构，而不是需要更宽模板的 PPM。

进行收敛研究时，还应检查密度、速度、内能或组分保护是否被触发，因为这些
保护可能改变无效或近真空状态下的更新。[参考手册](docs/Reference.zh-CN.md)
解释这些数值选择，验证页面则展示误差与守恒量的测量方法。

Release 构建保留 CPU 优化和 LTO。为维持所需的数值行为，共用构建设置会在
支持的 GNU、Clang 和 NVIDIA 工具链上关闭 fast-math 与浮点收缩；不过这不意味着
不同机器会产生逐位相同的结果。目前扩展功能使用源码接口，而非已安装的
二进制库接口。

## 许可证

ARCH 自有内容采用 [MIT License](LICENSE)。第三方衍生科学代码和数据保留其上游来源与条款，详见[第三方来源与说明](THIRD_PARTY_NOTICES.zh-CN.md)。MIT 许可证尤其不会重新许可 Timmes 衍生的反应网络、NSE 实现、Helmholtz EOS 或表数据。 可选 KLU 后端的 SuiteSparse LGPL/BSD 条款保留在 [LICENSES](LICENSES/) 与[第三方说明](THIRD_PARTY_NOTICES.zh-CN.md)中。

## 后续开发方向

下面两条路线展示已有功能之外的开发方向。每条路线的首项是当前推进重点；
带 `?` 的项目是后续候选方向，具体范围与设计仍可调整。箭头表示计划顺序，
不表示软件或物理上的依赖关系。

```text
物理：自引力 → MHD? → { BSSN? | Z4c? }
软件：MPI    → GNN? → { FP32/FP64 切换? | RT Core 加速? }
```

自引力将计算模拟物质自身产生的引力场，磁流体力学（MHD）则把磁场加入流体
模型。BSSN 和 Z4c 是未来可能考虑的广义相对论时空演化形式，目前作为候选
方案列出，并非已经实现的模块。

MPI 的方向是把模拟分配到多个进程和多台机器。后续还会探索图神经网络（GNN）、
32 位与 64 位浮点精度的选择，以及在适合的算法中利用 GPU 光线追踪核心
（RT Core）。这些计划与当前支持的功能分开列示。性能、内存使用、编译效率、
文档和验证也会持续优化，同时保持 CPU 与 GPU 共用一套数学和物理实现。
