# ARCH × CUDA × AMR：项目背景与知识学习路线

> 本文档的用途：交给 AI 导师（或自学时对照），系统补齐一个天体物理流体模拟 + GPU 加速项目所需的背景知识。
>
> **读者画像（重要，请 AI 导师据此调整讲法）**：读者是计算机科学研究生，有扎实的机器学习/深度学习背景（PyTorch、CUDA 生态的用户层经验、Python 熟练、C++ 能读能写），**但没有流体力学、数值 PDE、天体物理背景**。讲解时请多用类比（尤其可以类比 ML 中的概念，如"访存受限"可类比大模型推理的 memory-bound），公式要给直觉，避免堆砌推导。

---

## 一、项目背景

### 1.1 ARCH 是什么

ARCH（Adaptive Reactive CUDA Hydrodynamics）是一个用 C++17 从零编写的**恒星天体物理反应性可压缩流体力学模拟框架**，作者是我的合作者。它模拟的典型场景：白矮星表面/内部的碳爆轰（Ia 型超新星的核心过程）、瑞利-泰勒不稳定性、激波管等。

技术特征：
- **有限体积激波捕捉法**：Riemann 求解器可选（Lax-Friedrichs / Lax-Wendroff / Steger-Warming / van Leer / Roe / HLL / HLLC），重构可选（一阶 PCM / 二阶 MUSCL / 三阶 PPM），时间积分可选（Euler / RK2 / SSP-RK3）
- **网格**：1D/2D/3D 结构化均匀网格，支持直角/柱/球三种坐标系（目前**没有 AMR**，名字里的 Adaptive 尚未兑现——这正是我们的大项目）
- **物态方程（EOS）**：理想气体解析式，或查表插值（3D 表：ρ,e,X；4D 表：白矮星简并物质的 Helmholtz EOS）
- **核反应**：aprox19 网络（19 种核素，pynucastro 生成），与流体通过 Strang 算符分裂耦合，每个网格单元独立解一个刚性 ODE（后向欧拉 + Newton-Raphson + 稠密 LU）
- **工程架构**：policy 模板编译期组装（内层循环零虚函数）、SoA 数据布局、插件式算例注册、`.par` 运行时配置、HDF5 输出/断点重启
- **并行现状**：仅 OpenMP 多线程（CPU）。**代码里没有任何 CUDA，但数据布局和无状态内核设计是刻意为 GPU 移植预留的**

### 1.2 我们的改造计划（两阶段）

**阶段一（进行中）：CUDA 移植。** 把两大计算热点搬上 GPU：
1. 核燃烧内核：每格点独立的 19 组分刚性 ODE（每步多次 19×19 LU 分解）——天然大规模并行
2. 流体内核：重构 + Riemann 通量 + 时间推进的 stencil 计算——访存受限型

验收协议：同一配置文件 CPU/GPU 双跑，逐字段（ρ、动量、能量、组分）算 L∞/L2 相对误差（纯流体 ≤1e-13，燃烧组分 ≤1e-10），性能报 cell-updates/s。

**阶段二（大项目）：在 CUDA 框架上自研 block-structured AMR（自适应网格加密），性能目标远超 16 核 CPU。**
动机：爆轰波的反应区厚度比计算域小几个量级，均匀网格 99% 的格点是浪费；AMR 只在激波/火焰面附近加密。这是 FLASH/Castro/GAMER 等大型超新星模拟代码的标配能力，我们要做一个 GPU 原生的轻量实现。

### 1.3 硬件与基线

- GPU：NVIDIA H100-20C（vGPU 切片：**完整 114 个 SM，显存 20 GB**，compute capability 9.0；**不支持统一内存/managed memory**，必须显式 cudaMemcpy）
- CPU 基线（已验证）：Sod 激波管 2D 柱坐标 400×400、SSPRK3+HLLC+PPM，3873 步，16 线程约 33 秒
- 工具链：gcc 11.4 + CUDA 12.4 (nvcc) + CMake + HDF5

---

## 二、控制方程与变量（整个项目的数学心脏）

代码求解**反应性可压缩欧拉方程组**（守恒形式）：

```
∂U/∂t + ∇·F(U) = S(U)

守恒变量  U = (ρ,   ρu, ρv, ρw,   E,      ρXₖ)      k=1..19
通量      F = (ρv,  ρvv + pI,     (E+p)v,  ρXₖv)
源项      S = (0,   ρg,           ρv·g,    ρω̇ₖ)     ← 重力 + 核反应
闭合关系  p = p(ρ, e, Xₖ)                            ← 物态方程 EOS
```

- ρ 质量密度；ρu 动量密度（不是速度！）；E = ρe + ½ρ|v|² 总能量密度（内能+动能）；Xₖ 组分质量分数（∑Xₖ=1）
- **守恒变量 U vs 原始变量 W=(ρ,u,v,w,p)**：数值更新必须在 U 上做（保证激波速度正确，Rankine–Hugoniot 条件）；插值重构、EOS 调用、边界条件在 W 上做更自然。U↔W 互转依赖 EOS
- 左端（流动）是**双曲**的：信息以有限波速传播，解会自发产生间断（激波）——所以需要"激波捕捉"数值方法
- 右端核反应源项是**刚性**的：反应时标比流动时标快多个量级——所以用算符分裂（燃烧半步→流体整步→燃烧半步）+ 隐式 ODE 求解器

---

## 三、六大知识域与学习路线

> 建议顺序：① → ② 是主线必修（约占学习时间的 60%），③④ 按需，⑤⑥ 是大项目核心。
> 每个领域后面附有"自测问题"——能回答上来就算过关。

### ① 气体动力学基础（Gas Dynamics）

**为什么需要**：一切的物理直觉来源。不懂三种波，后面的 Riemann 求解器就是黑盒。

核心概念清单：
- 可压缩 vs 不可压缩流动；声速与马赫数
- 特征线与波的传播；双曲方程组的本质
- **三种基本波**：激波（shock，压缩间断）、接触间断（contact discontinuity，密度跳而压强速度不跳）、稀疏波（rarefaction，连续扩张）
- **Sod 激波管问题**：初始一隔膜分开高低压气体，撤膜后同时产生三种波——本领域的 "MNIST"
- Rankine–Hugoniot 跳跃条件（激波两侧守恒量的关系）

推荐资料：Toro 教材（见②）第 1–4 章；任何"气体动力学"讲义的激波章节。

自测问题：Sod 问题中为什么密度有两个间断而压强只有一个？激波和接触间断的本质区别是什么？

### ② 双曲守恒律的数值方法（Numerical Methods for Hyperbolic Conservation Laws）

**为什么需要**：ARCH 的整个 hydro 部分就是这套方法论的实现，逐文件对应。

核心概念清单：
- 有限体积法（FVM）：单元平均值 + 界面通量；为什么天然守恒
- **Godunov 方法**：把每个单元界面当成一个局部 Riemann 问题
- 近似 Riemann 求解器：Lax-Friedrichs（最耗散最稳）→ HLL → **HLLC**（恢复接触间断）→ Roe（线性化，需熵修正）
- 高阶重构：分片常数（1阶）→ MUSCL 线性重构+限制器（2阶）→ **PPM 分片抛物线**（3阶）
- TVD 性质与限制器（minmod / superbee / van Leer / MC）：为什么高阶格式在间断附近必须降阶
- **CFL 条件**：显式格式的时间步长稳定性约束 Δt ≤ CFL·Δx/max|波速|
- SSP Runge-Kutta 时间积分；算符分裂（Strang splitting，二阶精度）
- 曲线坐标系（柱/球）下的几何源项

推荐资料（按优先级）：
1. **E.F. Toro《Riemann Solvers and Numerical Methods for Fluid Dynamics》**（本项目圣经，代码几乎照此书实现）：重点第 2–6、10、13–14 章
2. R.J. LeVeque《Finite Volume Methods for Hyperbolic Problems》：更数学，选读
3. 动手验证：ARCH 仓库 `Validation_file/` 里有各求解器跑 Sod 的对比图

代码映射表（学一章对一个文件）：

| 教材内容 | ARCH 源文件 |
|---|---|
| Euler 方程、守恒变量 | `src/data/FluidState.h` |
| Godunov 框架、主时间循环 | `src/driver/Driver.h` |
| HLL/HLLC 求解器 | `src/numerics/flux/FluxHLL.h`, `FluxHLLC.h` |
| Roe 求解器与熵修正 | `src/numerics/flux/FluxRoe.h` |
| MUSCL/PPM 重构、限制器 | `src/numerics/reconstruction/Reconstruction.h`, `Limiters.h` |
| SSP-RK 时间积分 | `src/numerics/integrator/TimeIntegratorRK3.h` |
| CFL 时间步 | `src/driver/DriverUtils.h` (adaptive_dt) |

自测问题：为什么 Godunov 型格式要在守恒变量上更新而在原始变量上重构？HLLC 比 HLL 多恢复了哪种波？CFL=0.8 的物理含义？

### ③ 热力学与物态方程（EOS）

**为什么需要**：EOS 是 U↔W 转换的闭合关系，代码里无处不在；白矮星物质的 EOS 决定了爆轰物理。

核心概念清单：
- 理想气体：p = (γ-1)ρe；多组分混合的等效 γ
- 为什么白矮星不能用理想气体：**电子简并压**——压强来自泡利不相容原理而非温度（费米气体）
- Helmholtz EOS（Timmes）：以亥姆霍兹自由能为基础的表格化恒星 EOS，覆盖辐射压+理想离子+简并电子+库仑修正
- 表格插值的实现问题：维度（ρ, T, 平均原子量 Ā, 平均电荷 Z̄）、热力学一致性、插值阶数

推荐资料：Timmes & Swesty (2000, ApJS 126, 501)《The Accuracy, Consistency, and Speed of an Electron-Positron Equation of State...》；任何恒星结构教材的"简并物质"章节。

代码映射：`src/physics/eos/IdealGas.h`、`Tabular3DEOS.h`、`Tabular4DEOS.h`、`EOS_toolkit/`。

自测问题：为什么白矮星加热几乎不膨胀（从而导致热核失控）？EOS 表为什么常以 (ρ,T) 为自变量而代码却需要 (ρ,e) 查询？

### ④ 核天体物理：反应网络与核燃烧

**为什么需要**：燃烧内核是 GPU 移植的第一个目标；理解刚性 ODE 才能理解为什么它是计算热点。

核心概念清单：
- 核反应网络：组分丰度的 ODE 系统 dYₖ/dt = f(Y,ρ,T)；反应率的强温度依赖（~T^20 甚至更陡）
- aprox19：19 核素近似网络（H1→Ni56，α 链为主），超新星模拟的经典选择
- **刚性（stiffness）**：反应时标跨越多个量级 → 显式方法步长被最快反应锁死 → 必须隐式
- 后向欧拉 + Newton-Raphson；Jacobian 矩阵；每次牛顿迭代解一个 19×19 稠密线性系统（LU 分解）
- 算符分裂耦合：燃烧(dt/2) → 流体(dt) → 燃烧(dt/2)；能量反馈（核能→内能→温度→压强）
- 电子屏蔽（screening）、配分函数、弱相互作用速率表

推荐资料：Timmes (1999, ApJS 124, 241)《Integration of Nuclear Reaction Networks for Stellar Hydrodynamics》；pynucastro 文档。

代码映射：`src/numerics/burnsolver/`（BE-NR 求解器）、`src/physics/network/aprox19/`（网络右端项与 Jacobian，pynucastro 自动生成）、`src/driver/Driver.h` 的 do_burn_step。

自测问题：为什么燃烧 ODE 不能用 RK4 硬积？一个 1000 万格点、每格点平均 5 次牛顿迭代的燃烧步，浮点运算量大约多少？

### ⑤ 自适应网格加密 AMR（大项目的灵魂）

**为什么需要**：这就是大项目本身。

核心概念清单（按这个顺序理解）：
- **动机**：爆轰反应区 / 火焰面 / 激波是局部薄结构，均匀细网格代价 O(N³)——AMR 只在需要处加密
- **block-structured AMR（Berger–Oliger / Berger–Colella 谱系）**：网格组织成固定大小的块（如 8³/16³），块可整体加密为 2× 分辨率的子块，形成层级（vs 逐点加密的 octree cell-based 方案；GPU 上 block-based 是主流，因为一个 GPU 线程块正好处理一个网格块）
- **加密判据（refinement criteria）**：密度/压强梯度、组分梯度、燃烧率、Löhner 误差估计
- **prolongation / restriction**：粗→细插值（守恒插值）与细→粗平均
- **ghost cell 填充**：同层邻块交换、跨层插值
- **reflux（通量修正）**：粗细界面上粗网格通量 ≠ 细网格通量之和 → 必须事后修正，否则破坏守恒（直接呼应②中"守恒"概念——这是 AMR 正确性的命门）
- **时间 subcycling**：细层走 2 个 dt/2，粗层走 1 个 dt；各层 CFL 独立
- 负载均衡与空间填充曲线（Morton/Z-order、Hilbert）——块到 GPU/线程块的映射

推荐资料（按顺序）：
1. Berger & Colella (1989, JCP 82, 64)《Local Adaptive Mesh Refinement for Shock Hydrodynamics》——开山之作，必读
2. AMReX 官方文档的 "AMR 核心概念" 章节（把上面论文翻译成了工程语言，免费在线）
3. GPU-AMR 三大先例论文：
   - GAMER-2：Schive et al. (2018, MNRAS 481, 4815)——octree 块结构 AMR 全 GPU 化的典范
   - AMReX：Zhang et al. (2021, IJHPCA)《AMReX: Block-structured adaptive mesh refinement for multiphysics applications》
   - Parthenon：Grete et al. (2023, IJHPCA)——performance-portable AMR 框架（Athena++ 谱系）
4. 应用参考：CASTRO（Almgren et al. 2010, ApJ 715, 1221）——AMReX 上的超新星反应流代码，正是 ARCH 的"满配版"

自测问题：为什么粗细界面不 reflux 会丢质量？为什么 GPU 上偏爱固定块大小的 block-based 而不是 cell-based octree？subcycling 时细层的 ghost cell 需要粗层什么时刻的数据（提示：时间插值）？

### ⑥ GPU 高性能计算（stencil 与 many-ODE 两种范式）

**为什么需要**：性能"远超 CPU"的兑现手段；你的 ML 背景在这里最能迁移。

核心概念清单：
- **访存受限 vs 计算受限**（roofline 模型）：hydro stencil 每格点算术少、要读邻居 → 受限于显存带宽（H100 HBM ≈ CPU DDR 的 ~10 倍 → 这就是性能上限的来源，类比 LLM 推理的 memory-bound decode）
- SoA 布局与合并访存（coalescing）——ARCH 已是 SoA，天然友好
- stencil 内核优化：shared memory tiling、线程块尺寸选择、occupancy、寄存器压力
- 燃烧内核的特殊性：每线程一个独立刚性 ODE → **计算受限 + 分支发散**（不同格点牛顿迭代次数不同）→ warp 内负载不均是主要损失；缓解：按"是否点火"分桶、每线程处理一个格点而不是协作
- 归约（CFL 求全域最大波速）：块内归约 + atomic / CUB
- 主机-设备数据管理：本项目 vGPU **无 managed memory**，必须显式 cudaMalloc/cudaMemcpy；目标是整个时间循环驻留 GPU，仅 IO 时回传
- 双精度：科学计算用 FP64；H100 的 FP64 吞吐与 Tensor Core 无关（不要被 ML 的 TF32/FP8 直觉带偏）
- 工具：Nsight Compute/Systems 剖析、compute-sanitizer 查错

推荐资料：CUDA C++ Programming Guide（官方）；《Programming Massively Parallel Processors》(Kirk & Hwu) 前半本；NVIDIA 技术博客的 stencil/finite-difference 系列。

自测问题：为什么 hydro 内核的理论加速比 ≈ 带宽比而不是核心数比？burn 内核里 warp divergence 从哪来、怎么缓解？

---

## 四、建议学习节奏（供参考，可压缩）

| 阶段 | 内容 | 产出/自测 |
|---|---|---|
| 第 1 周 | ①气体动力学 + ②FVM 前半（Godunov、HLL/HLLC） | 能手推 Sod 三波结构；能解释 HLLC 每一步 |
| 第 2 周 | ②后半（PPM、限制器、RK3、CFL）+ ③EOS | 对照 ARCH 源码能讲出每个文件在干嘛 |
| 第 3 周 | ⑤AMR（Berger–Colella + AMReX 文档 + GAMER-2 论文） | 能画出两层 AMR 一个时间步的完整流程图（含 reflux 与 subcycling） |
| 第 4 周 | ⑥GPU 优化 + ④核网络（按需） | 能预估 hydro 内核在 H100 上的带宽上限性能 |

---

## 五、术语对照表（方便中英文检索）

| 中文 | English |
|---|---|
| 可压缩欧拉方程组 | compressible Euler equations |
| 双曲守恒律 | hyperbolic conservation laws |
| 有限体积法 | finite volume method (FVM) |
| 激波捕捉 | shock capturing |
| 黎曼问题/求解器 | Riemann problem / solver |
| 接触间断 / 稀疏波 | contact discontinuity / rarefaction wave |
| 重构 / 限制器 | reconstruction / (slope) limiter |
| 分片抛物线法 | piecewise parabolic method (PPM) |
| 物态方程 | equation of state (EOS) |
| 电子简并压 | electron degeneracy pressure |
| 核反应网络 | nuclear reaction network |
| 刚性常微分方程 | stiff ODE |
| 算符分裂 | operator splitting (Strang) |
| 自适应网格加密 | adaptive mesh refinement (AMR) |
| 块结构 AMR | block-structured AMR |
| 加密判据 | refinement criteria |
| 粗细插值/限制 | prolongation / restriction |
| 通量修正 | refluxing / flux correction |
| 时间细分 | (time) subcycling |
| 幽灵单元 | ghost cells |
| 合并访存 | memory coalescing |
| 访存受限 | memory-bound |
| 占用率 | occupancy |
| 胞状爆轰 | cellular detonation |
| Ia 型超新星 | Type Ia supernova |
| 核坍缩超新星 | core-collapse supernova (CCSN) |

---

## 六、可以直接抛给 AI 导师的问题清单

入门（对应①②）：
1. 请用 Sod 激波管为例，讲解可压缩欧拉方程的三种波，并解释为什么数值格式必须写成守恒形式
2. 请一步步推导/讲解 HLLC 近似黎曼求解器，说明它相比 HLL 的改进
3. 什么是 TVD 和限制器？为什么高阶格式在激波附近必须"降阶"？
4. PPM 相比 MUSCL 高明在哪里？代价是什么？

进阶（对应③④）：
5. 白矮星物质的物态方程和理想气体有什么本质区别？简并压如何导致热核失控？
6. 为什么核反应网络是刚性的？后向欧拉+牛顿迭代如何解它？Jacobian 在其中扮演什么角色？

大项目（对应⑤⑥）：
7. 请详细讲解 Berger–Colella block-structured AMR 的一个完整时间步：加密判据→建层→ghost 填充→各层推进→subcycling→restriction→reflux
8. 为什么 AMR 不做 reflux 会破坏守恒？请用一个粗细界面的通量图说明
9. GPU 上实现 AMR，block-based 和 cell-based(octree) 各有什么取舍？GAMER-2 和 AMReX 分别怎么做的？
10. 一个访存受限的 stencil 内核，如何用 roofline 模型估计它在 H100 上的性能上限？shared memory tiling 什么时候有用什么时候没用？

---

*文档生成：2026-07-12 · 项目工作区：jaist-gpu:~/ARCHcuda · CPU 基线：Sod 2D 400×400 SSPRK3+HLLC+PPM, 3873 步 33s@16 线程*
