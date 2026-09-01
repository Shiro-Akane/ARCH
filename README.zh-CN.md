# ARCH：自适应反应流 CUDA 流体力学框架

英文原文：[README.md](README.md)。英文版是唯一规范文本；行为或接口发生变化时应先更新英文版。若中英文内容不一致，以英文版为准。

[![C++20](https://img.shields.io/badge/standard-C%2B%2B20-blue.svg)]()
[![Build](https://img.shields.io/badge/build-CMake-orange.svg)]()
[![CPU backend](https://img.shields.io/badge/backend-CPU-success.svg)]()
[![CUDA](https://img.shields.io/badge/CUDA-experimental-yellow.svg)]()
[![ARCH code: MIT](https://img.shields.io/badge/ARCH_code-MIT-yellow.svg)](LICENSE)

ARCH 是一个面向可压缩反应流体力学的块自适应有限体积框架。CPU 后端是科学语义权威；使用 `ARCH_ENABLE_CUDA=ON` 构建时，还会提供实验性的 uniform-Cartesian 后端。该后端复用同一套注册策略与无分配物理数学实现。

项目主要面向两类使用者：

- 学习如何配置、初始化、推进和检查 CFD 算例的学生；
- 直接在源码树中扩展 Riemann 求解器、状态方程、核反应网络、扩散、重力、AMR 或诊断功能的科研人员。

## 项目状态

| 功能 | CPU 后端 | CUDA 后端 |
| --- | --- | --- |
| 1D/2D/3D 流体力学 | 支持 | 实验性；仅 uniform Cartesian 拓扑 |
| 动态块 AMR | 支持 | 明确拒绝；device storage transaction 只是基础设施，不代表 AMR 已接入 |
| 理想、表格和 Helmholtz EOS | 支持 | 已实现；若干表格路径仍缺 device 资格验证 |
| RKL1/RKL2 扩散 | 支持 | Cartesian 网格已实现 |
| 重力 | none 与 external | 仅 none；external/self 会被拒绝 |
| 核反应网络与 NSE | 内置与生成网络；仅四个内置网络支持 NSE；DenseLU/可选 KLU | 四个内置网络、NSE 与 DenseLU（最多 30 个核素）；不支持生成网络和稀疏求解 |
| Plot/checkpoint 写出 | 共用 host writer | 复用同一 writer；device 为权威时先显式 materialize 到 host |
| Checkpoint 重启 | 支持 | 明确拒绝；`auto` 可在构造前回退 CPU |
| 定量验证套件 | 已记录 CPU 基线 | CPU/CUDA 一致性仍待完成 |

`compute_backend = cpu`、`cuda` 或 `auto` 会在后端构造前一次性解析。显式 CUDA 不会静默回退；只有 `auto` 可以在请求超出 CUDA 能力矩阵或没有可用设备、且 CPU capability gate 接受同一运行时，于构造前选择 CPU。

后端分离被刻意限制在必要范围：策略注册、AMR 决策、host-only Morton/拓扑数学、EOS/网络/求解器数学、调度与 HDF5 schema 全部共用；CUDA 专用部分只负责 kernel、device storage、传输、stream 和 retirement fence。

## 已实现功能

- 带 ghost exchange、prolongation/restriction、flux register 和 reflux 的守恒块 AMR；
- Cartesian、cylindrical 和 spherical 网格上的维度感知 1D、2D 和 3D 存储；
- SW、VL、Roe、HLL 和 HLLC 通量策略；
- PCM、MUSCL/PLM 和 PPM 重构，以及 Euler、SSPRK2 或 SSPRK3 时间推进；
- 理想气体、自动识别维数的 3D/4D 表格（包括规范化自由能表）和 Timmes Helmholtz 状态方程；
- 外部重力、内置或 pynucastro 生成的核燃烧、DenseLU/KLU 线性求解、NSE 投影和 RKL1/RKL2 超时间步扩散；
- HDF5 plot/checkpoint 文件，包括 AMR 叶节点层次的重启。

## 构建

ARCH 面向 Linux/WSL 风格的 C++ 环境。所需工具和库包括：

- C++20 编译器；
- CMake 3.22 或更高版本；
- OpenMP，除非配置时关闭；
- HDF5 C++ 和 HL 库；
- Git，以及配置阶段的网络访问，因为 CMake 会获取 HighFive，并在找不到已安装 KLU package 时获取固定的 SuiteSparse；
- Git LFS，用于克隆由 LFS 管理的 EOS `.dat` 或 `.h5` 资源。

在仓库根目录配置和构建：

```bash
git lfs pull
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 4
```

实验性 CUDA 后端需要 CUDA toolkit，以及 compute capability 不低于 8.6 的设备：

```bash
cmake -S . -B build-cuda -DARCH_ENABLE_CUDA=ON -DARCH_ENABLE_KLU=OFF
cmake --build build-cuda --parallel 2
```

KLU 仍是 CPU-only 的可选后端；cuDSS 尚未接入。

dispatch 翻译单元会实例化较大的模板组合矩阵。即使该目标以较低优化级别编译，过高的并行构建数仍可能耗尽内存。只有在确认可用内存后才提高 `--parallel`。

可执行文件写入 `bin/ARCH`。KLU 默认启用；CMake 会使用已安装的 package，或获取固定的 SuiteSparse v7.13.0。

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

生成 custom 网络时复制示例 recipe，自行选择唯一的 `NETWORK_ID` 和核素：

~~~bash
cp examples/network/CustomNetworkRecipe.py MyNetwork.py
conda run -n p311 python tools/network/GenerateNetwork.py MyNetwork.py --check
conda run -n p311 python tools/network/GenerateNetwork.py MyNetwork.py
cmake -S . -B build
cmake --build build --parallel 4
~~~

每个生成 package 位于 `src/physics/network/custom/<id>/`。CMake 注册该目录内
实际存在的 ID，并允许多个 ID 共存；`aprox*`/`iso*` 命名空间保留给内置网络。
已有 ID 的替换由生成器的受保护流程管理。每次运行选择一个 package：

~~~text
network_name = custom:<id>
use_burn = true
use_nse = false
linear_solver = Auto
~~~

生成网络当前仅支持 CPU，且不实现 Timmes NSE 投影。`Auto` 在不超过 30 个核素时
保留专用 DenseLU 路径；超过 30 个核素时，只有启用了 KLU 的 CPU build 才会选择
SparseKLU。
完整契约和生成器限制见[研究与 API 参考](docs/Reference.zh-CN.md)，多规模网络
兼容证据统一见 [validation/network](validation/network/README.zh-CN.md)。

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

定量状态、CPU 结果、已知失败和 CUDA 占位项集中在[验证索引](validation/README.zh-CN.md)。[AMR 状态页](validation/amr/README.zh-CN.md)整合历史图像、定量守恒基线和仍存在的局部细化保持限制。

## 仓库结构

```text
ARCH/
├── README.md                  # 英文入口与首次运行，规范文本
├── README.zh-CN.md            # 中文辅助入口
├── LICENSE                    # ARCH 自有内容的 MIT 许可证
├── THIRD_PARTY_NOTICES.md     # 科学软件来源与第三方条款
├── LICENSES/                  # 保留的第三方许可证文本
├── CMakeLists.txt             # CPU 构建和模板 dispatch 目标
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
- CUDA 一致性、动态 CUDA AMR、CUDA restart 读入、非 Cartesian CUDA 几何、CUDA 重力、生成式 CUDA 网络和 CUDA 稀疏求解仍不属于已验证功能集；当前 KLU 与生成式网络证据均为 CPU-only，并统一索引在 `validation/network`；
- Release 构建使用 `-march=native` 和 `-ffast-math`，优先性能而不是跨机器逐位复现；
- ARCH 当前提供源码级扩展接口，而不是已安装的公共库 ABI。

收敛和生产研究应遵循 Reference。正式验证结论必须包含可复现输入、参考解、范数和容差。

## 许可证

ARCH 自有内容采用 [MIT License](LICENSE)。第三方派生科学代码和数据保留其上游来源与条款，详见[第三方来源与说明](THIRD_PARTY_NOTICES.zh-CN.md)。MIT 许可证尤其不会重新许可 Timmes 派生的反应网络、NSE 实现、Helmholtz EOS 或表数据。 可选 KLU 后端的 SuiteSparse LGPL/BSD 条款保留在 [LICENSES](LICENSES/) 与[第三方说明](THIRD_PARTY_NOTICES.zh-CN.md)中。
