# CUDA 后端与 GPU-AMR 能力

英文原文：[CudaBackendStatus.md](CudaBackendStatus.md)。英文版是规范文本。

CUDA 后端在 GPU 上执行流体与自适应网格数值工作。CPU/CUDA 共用数学和物理
实现；设备内存、计算核、执行流和线性求解库由各后端管理。
两个后端均支持下述功能范围。受测配置、技术结果和整体验收状态统一记录在
[验证索引](../validation/README.zh-CN.md)中。

## 共用功能范围

| 模块 | 已有实现范围 |
|---|---|
| 流体 | Van Leer、Steger-Warming、Roe、HLL、HLLC；PCM/MUSCL/PPM；MinMod/MC/SuperBee/Van Leer 限制器；Euler/SSPRK2/SSPRK3 |
| 网格与几何 | 一、二、三维块网格；笛卡尔、柱坐标与球坐标，采用两端共用的坐标约定 |
| 边界 | 周期、流出、反射 |
| 动态 AMR | 细化指标、守恒插值与限制、跨层交换、流体／扩散通量修正和事务式状态迁移 |
| EOS | 理想气体、Helmholtz、规范化 Tabular3D/Tabular4D 数据布局 |
| 扩散 | 组分、热、黏性模式及 RKL1/RKL2 |
| 重力 | 使用共用分阶段源项的外部重力 |
| 内置燃烧 | iso7、aprox13、aprox19、aprox21；BE_NR、BD、ROS4 和网络受限 NSE |
| 生成网络燃烧 | 已注册且数学实现可在设备端运行的网络；支持已识别的内嵌弱反应率表，各后端分别保存只读数据；按下述规则执行稠密或稀疏求解 |
| 输出与恢复 | 共用 HDF5/checkpoint 设施，在 IO 边界同步状态，并提供 CPU/CUDA 重启路径 |

策略名称、别名和能力判定来自同一注册系统；EOS／网络采用编译期鸭子类型
（duck typing）接口。后端适配层只连接存储或求解器，不另建物理模型。具体配置见
[API 与参数参考](Reference.zh-CN.md)。

## GPU-AMR 执行方式

实现入口见 [CUDA runtime](../src/cuda/runtime/README.md)和[共用 AMR 模块](../src/amr/README.md)。

CPU 负责拓扑、Morton 排序和细化/合并决策。GPU 计算单元指标，仅向 CPU 回传
每块一个汇总值；场数据的守恒迁移在设备缓冲区上执行，复用 CPU 的数值公式。
因此，网格决策由 CPU 控制，大批量的场计算与迁移留在 GPU。
写出检查点或绘图数据时，所需字段会复制到共用的主机写入器。

## 稠密与稀疏燃烧求解

ODE 方程数为核素数加温度方程；需要积分有符号弱反应损失的网络另含一个能量源状态。
`linear_solver = Auto` 下，总数不超过 31 个方程使用共用 DenseLU
（不含辅助状态时最多 30 核素，含该状态时最多 29 核素）；更大系统选择 CPU KLU 或 GPU cuDSS。
相应求解库及网络／EOS 执行代码必须已构建。显式指定 CPU+cuDSS 或 CUDA+KLU
会被拒绝；显式 CUDA 不会静默回退到 CPU 物理计算。
`compute_backend = auto` 可在启动时选择可用且支持该配置的后端并报告选择，
进入构造后不再切换。

CUDA 稀疏执行器让数值状态和 CSR 矩阵保留在 GPU，主机调用 cuDSS API 并交换
执行请求和响应。矩阵分解的存储设有上限，内存需求取决于分解产生的非零项和实际工作负载。
当前适配层使用 cuDSS 0.8 API 和非对称 BTF/COLAMD 排序，并对原始矩阵
检查修正量残差。原生执行/资源错误会明确报告，不伪装成 ODE 重试。

ARCH 支持 pynucastro 生成的反应网络，并通过 CPU KLU / GPU cuDSS 提供稀疏求解。
对于超大网络，具体模型的科学可靠性取决于核素集合、反应数据和适用范围；
资源需求随网络与网格规模增长。

## 网络和表格数据的选择

- 生成的弱反应率表在两侧共用插值、导数和能量积分。生成器会检查表格布局并
  报告不支持的输入。版本 3 或尚未转换为设备端实现的网络包仍只能在 CPU 上运行；
  CUDA 需要版本 4 网络包，并在清单中声明 `device_callable_math=true`。
  旧网络包需使用当前生成器重新生成，才能使用其支持的 CUDA 接口。
  独立 Urca 轨迹结果见[网络验证](../validation/network/README.zh-CN.md)。
- 自引力、自定义网络 NSE 均不是两端现有生产功能。内置 NSE 受所选核素集合限制，
  alpha-chain 网络不能替代通用 NSE 网络。
- ARCH 规范化 EOS 表不能直接替换为任意原生核物质表；单位、热力学分量与能量
  零点必须满足[表格契约](../src/physics/eos/TabularEOS.zh-CN.md)。
- 本地发布验证使用有代表性的生成网络和弱反应网络。更大模型的轨迹和资源
  测量安排在适合其规模的机器上。ARCH 当前采用单机 CPU 或单 GPU CUDA 执行。

[验证索引](../validation/README.zh-CN.md)区分数值结果与实现声明，并提供可复现的
输入和指标。贡献者实验日志和机型相关证据单独保留在
[开发记录](development/README.md)，不混入本功能指南。
