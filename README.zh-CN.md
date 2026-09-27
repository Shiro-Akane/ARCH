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

从下方的[构建](#构建)与[首次运行](#首次运行)开始；了解可用模块及组合条件时，阅读[功能清单](docs/Features.zh-CN.md)和[参考手册](docs/Reference.zh-CN.md)。

## 功能与文档

ARCH 提供一至三维流体、动态 AMR、状态方程、扩散、核反应及外部／自引力计算，并可在 CPU 或 CUDA 后端运行。[独立功能清单](docs/Features.zh-CN.md)列出各模块、几何与边界的适用范围。[验证索引](validation/README.zh-CN.md)说明经过检查的模型组合；[版本说明](docs/releases/README.md)记录各源码版本的变化。

在参数文件中用 `compute_backend = cpu`、`cuda` 或 `auto` 选择后端。自动选择只发生在启动阶段；细节见[CUDA 指南](docs/CudaBackendStatus.zh-CN.md)。

## 构建

在 Linux 或 WSL2 终端中构建。CPU 需要支持 C++20 的编译器、CMake 3.22+、Ninja、HDF5 C++/HL、OpenMP 和 Git；CUDA 还需要 CMake 3.25.2+、CUDA Toolkit 12.0+ 与兼容的驱动。依赖安装、内存限制和源码包中的 EOS 表处理见[构建指南](docs/guides/Build.zh-CN.md)。

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

## 扩展模型

表格 EOS 可以加载符合[数据契约](src/physics/eos/TabularEOS.zh-CN.md)的表。自定义反应网络通过[生成网络指南](src/physics/network/custom/README.md)建立，并在构建时注册。它们各自有物理数据、材料与求解器条件；[参考手册](docs/Reference.zh-CN.md)列出配置参数与组合规则。

## 文档总览

[文档入口](docs/README.zh-CN.md)按学习、配置、验证和开发任务组织内容。[算例指南](docs/guides/SimulationCase.zh-CN.md)介绍如何查看输出与编写 `Setup`／`Init`；[参考手册](docs/Reference.zh-CN.md)提供参数与接口；[验证索引](validation/README.zh-CN.md)说明已测范围。问题反馈见[科研计算指南](docs/guides/Reporting.zh-CN.md)。

## 仓库结构

```text
ARCH/
├── simulation/  # 可运行算例与示例输入
├── src/         # 流体、物理、数值方法和后端实现
├── EOS_toolkit/ # 运行时表数据
├── docs/        # 指南、功能清单和参考手册
├── validation/  # 科学验证与结果
├── tests/       # 回归测试
├── tools/       # 构建和验证工具
└── cmake/       # 构建配置
```

模块入口见[源码导览](src/README.md)。

## 数值适用范围

耦合计算同时受流体、燃烧、扩散与自引力各自的时间精度和物理模型限制。具体的组合条件、状态修复与守恒诊断见[参考手册](docs/Reference.zh-CN.md)；受测配置及误差见[验证索引](validation/README.zh-CN.md)。

## 许可证

ARCH 自有代码采用 [MIT License](LICENSE)。第三方科学代码和数据保留其来源与条款，见[第三方声明](THIRD_PARTY_NOTICES.zh-CN.md)及[许可证目录](LICENSES/)。
