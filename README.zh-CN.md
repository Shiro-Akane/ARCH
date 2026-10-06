# ARCH：自适应反应流 CUDA 流体力学框架

英文原文：[README.md](README.md) · [文档总览](docs/README.zh-CN.md)

[![C++20](https://img.shields.io/badge/standard-C%2B%2B20-blue.svg)]()
[![Build](https://img.shields.io/badge/build-CMake-orange.svg)]()
[![CPU CI](https://github.com/Shiro-Akane/ARCH/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Shiro-Akane/ARCH/actions/workflows/ci.yml)
[![CPU backend](https://img.shields.io/badge/backend-CPU-success.svg)]()
[![CUDA](https://img.shields.io/badge/CUDA-supported-success.svg)](docs/CudaBackendStatus.zh-CN.md)
[![ARCH code: MIT](https://img.shields.io/badge/ARCH_code-MIT-yellow.svg)](LICENSE)

ARCH 用于模拟可压缩流体运动、传热与核反应。它以有限体积法推进流体状态，并用自适应网格细化（AMR）增加局部空间分辨率。CPU 与 CUDA 共用物理和数学实现。

**ARCH 的输入、输出和物理常数统一使用 CGS**：长度为 `cm`，时间为 `s`，密度为 `g/cm³`，压力和能量密度为 `erg/cm³`，比内能为 `erg/g`，温度为 `K`。角度使用 `rad`。输入不会自动换算单位；更多量纲见[算例指南](docs/guides/SimulationCase.zh-CN.md#单位)。

从下方的[构建](#构建)与[首次运行](#首次运行)开始。完成第一个算例后，按[ARCH 模拟算例指南](docs/guides/SimulationCase.zh-CN.md)继续学习；需要查找模块范围或具体参数时，再使用[功能清单](docs/Features.zh-CN.md)和[参考手册](docs/Reference.zh-CN.md)。

## 功能与文档

ARCH 提供一至三维流体、动态 AMR、状态方程、扩散、核反应及外部／自引力计算，并可在 CPU 或 CUDA 后端运行。[独立功能清单](docs/Features.zh-CN.md)列出各模块、几何与边界的适用范围。[验证索引](validation/README.zh-CN.md)说明经过检查的模型组合；[版本说明](docs/releases/README.md)记录各源码版本的变化。

物理域和自引力支持独立的用户边界回调，使用同目录源码及现有两个公共头文件；
入流、热通量与势边界的写法见[用户边界指南](docs/guides/UserBoundaries.zh-CN.md)。

在参数文件中用 `compute_backend = cpu`、`cuda` 或 `auto` 选择后端。自动选择只发生在启动阶段；细节见[CUDA 指南](docs/CudaBackendStatus.zh-CN.md)。

## 构建

本地桌面 [ARCH Studio](docs/guides/Studio.zh-CN.md) 提供分组参数编辑、真实初始场与 AMR 预览、受控编译、独立终端运行/续算以及只读 Plotfile 查看。可用 `ARCH_BUILD_STUDIO=ON` 与 Core 一起构建，随后通过 `arch-studio` 呼出；每个模型的能力以选中二进制返回的接口为准。[Studio 状态](studio/STATUS.md)与[本轮发布收束计划](docs/development/ComputeStudioReleasePlan-20261006.zh-CN.md)列出验收范围及待完成内容。

在 Linux 或 WSL2 终端中构建。按所选后端准备依赖：

- **基础工具：** 支持 C++20 的编译器、CMake 3.22+、Ninja 和 Git。
- **两种后端共用的库：** HDF5 C++/HL 和 OpenMP。
- **CUDA 构建另需：** CMake 3.25.2+、CUDA Toolkit 12.0+ 和兼容的 GPU 驱动。

依赖安装、编译内存限制及源码包中的 EOS 表处理见[构建指南](docs/guides/Build.zh-CN.md)。

### 获取源码

```bash
git clone --branch main --single-branch https://github.com/Shiro-Akane/ARCH.git
cd ARCH
```

已有检出可以在保存本地修改后用 `git pull --ff-only` 更新。

### 方案 A：使用 CPU

```bash
cmake --preset cpu-release
cmake --build build-cpu --target ARCH --parallel 1
```

程序位于 `build-cpu/bin/ARCH`，可以直接进行[首次运行](#首次运行)。

### 方案 B：同时支持 CPU 与 CUDA

在将要运行计算的 GPU 机器上执行：

```bash
cmake --preset cuda-release
cmake --build build-cuda --target ARCH --parallel 1
```

程序位于 `build-cuda/bin/ARCH`，也可以执行 CPU 算例。CUDA 编译需要更多主机内存与时间；资源受限时按[构建指南](docs/guides/Build.zh-CN.md)使用编译保护工具和选择并行数。稀疏 CUDA 燃烧需要可选的 cuDSS 库，配置方法也在该指南中。

首次运行的 Sod 算例无需 EOS 表。Helmholtz 等算例需要真实表数据；Git 检出可运行 `git lfs pull`，源码归档的处理见[表格说明](docs/guides/Build.zh-CN.md#源码包与-eos-表)。

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
将副本中已有的 `compute_backend = cpu` 替换为 `compute_backend = cuda`，再运行该副本。不要追加第二次赋值：重复键会明确报错。

示例已显式提供必需配置。修改后可先进行只读检查：

    ./build-cpu/bin/ARCH --inspect-config Sod --config-stdin < simulation/Sod/Sod_beginner.par

该检查仅覆盖已声明配置，不执行 Setup、不加载 EOS、不检查路径存在性，也不代表已经具备模拟运行条件。缺失或非法值需要补齐或修正，不会从运行默认值静默回填。

成功运行后，程序会打印所选方法和时间步表，并在 `output/first_sod/` 下写入：

```text
SodBeginner_log.dat
SodBeginner_HLLC_plt_0000.h5
SodBeginner_chk_0000.h5
...
```

日志是可以直接阅读的文本；名称包含 `_plt_` 的文件保存供查看的流体场，
`_chk_` 文件则是用于重启的检查点。HDF5 是这些二进制数据文件采用的格式。这个示例使用 64 个单元；
`simulation/Sod/Sod.par` 提供 128 单元的标准激波管，
`simulation/Sedov/` 则提供爆炸波示例。

**初学者下一步：** 请从[ARCH 模拟算例指南](docs/guides/SimulationCase.zh-CN.md)继续。它以刚运行的 Sod 算例为起点，按顺序解释网格与参数、如何检查输出和进行受控实验，最后带你编写自己的算例。

## 扩展模型

表格 EOS 可以加载符合[数据契约](src/physics/eos/TabularEOS.zh-CN.md)的表。自定义反应网络通过[生成网络指南](src/physics/network/custom/README.md)建立，并在构建时注册。它们各自有物理数据、材料与求解器条件；[参考手册](docs/Reference.zh-CN.md)列出配置参数与组合规则。

## 文档总览

[文档入口](docs/README.zh-CN.md)按学习、配置、验证和开发任务组织内容。[算例指南](docs/guides/SimulationCase.zh-CN.md)介绍如何查看输出与编写 `Setup`／`Init`；[参考手册](docs/Reference.zh-CN.md)提供参数与接口；[验证索引](validation/README.zh-CN.md)说明已测范围。问题反馈见[科研计算指南](docs/guides/Reporting.zh-CN.md)。

## 仓库结构

[源码导览](src/README.md)说明模块职责和入口。本图方便首次定位；接口与审阅规则见[开发者指南](docs/development/README.md)。

```text
ARCH/
├── include/                 # 用户算例所需的两个公开头文件
├── simulation/              # 可运行算例与示例输入
├── src/
│   ├── api/                 # GUI 所用配置检查与 CPU 预览接口
│   ├── core/                # 参数定义、解析与算例注册
│   ├── interface/           # 算例设置和初态适配
│   ├── data/                # 场、状态与配置数据类型
│   ├── grid/                # 坐标与有限体积几何
│   ├── amr/                 # 网格层次、迁移、交换与通量修正
│   ├── driver/              # 运行时选择、阶段调度和状态生命周期
│   ├── cuda/                # 设备存储、计算核与后端适配
│   ├── numerics/            # 共用数值方法
│   │   ├── flux/            # Riemann 与通量分裂策略
│   │   ├── reconstruction/  # 面状态与斜率限制
│   │   ├── integrator/      # 流体时间推进
│   │   ├── diffusion/       # 扩散算子与 RKL 推进
│   │   ├── burnsolver/      # 反应网络 ODE 积分
│   │   ├── linalg/          # 线性系统视图与求解器
│   │   ├── elliptic/        # 泊松算子与边界离散
│   │   ├── multigrid/       # 多重网格层次、迁移与循环
│   │   └── state/           # 状态可接受性检查
│   ├── physics/             # 共用物理模型与材料数据
│   │   ├── eos/             # 热力学闭合与表格读取
│   │   ├── gravity/         # 外部引力与自引力物理
│   │   ├── network/         # 内置与生成的反应网络
│   │   ├── nse/             # 核统计平衡
│   │   ├── species/         # 组分与混合物性质
│   │   ├── diffusionCoe/    # 输运系数
│   │   ├── constant/        # 物理常数与单位
│   │   └── diagnostics/     # 派生物理诊断
│   ├── io/                  # 日志、HDF5 场输出与检查点
│   └── main.cpp             # 程序入口
├── EOS_toolkit/             # 运行时 EOS 表
├── docs/                    # 指南、功能清单和参考手册
├── validation/              # 科学验证与结果
├── tests/                   # 回归测试
├── tools/                   # 构建和验证工具
└── cmake/                   # 构建配置
```

## 数值适用范围

耦合计算同时受流体、燃烧、扩散与自引力各自的时间精度和物理模型限制。具体的组合条件、状态修复与守恒诊断见[参考手册](docs/Reference.zh-CN.md)；受测配置及误差见[验证索引](validation/README.zh-CN.md)。

## 后续开发方向

下面是[现行功能](docs/Features.zh-CN.md)之外的候选路线。问号表示设计范围仍可调整；箭头表示规划顺序，不表示软件依赖。

```text
物理：引力模型扩展? → MHD? → { BSSN? | Z4c? }
软件：MPI → GNN? → { FP32/FP64 切换? | RT Core 加速? }
```

自引力已覆盖[功能清单中的受测计算域](docs/Features.zh-CN.md#自引力计算域)；域外质量源仍是可能的扩展。磁流体力学（MHD）会把磁场加入流体模型。BSSN 和 Z4c 是未来时空演化的候选形式。

MPI 用于将计算分配到多个进程和机器。后续探索还可能包括图神经网络（GNN）、浮点精度选择，以及在适合的算法中使用 GPU 光线追踪核心（RT Core）。数值精度、性能、内存、编译效率和文档也会随项目持续改进。

## 许可证

ARCH 自有代码采用 [MIT License](LICENSE)。第三方科学代码和数据保留其来源与条款，见[第三方声明](THIRD_PARTY_NOTICES.zh-CN.md)及[许可证目录](LICENSES)。
