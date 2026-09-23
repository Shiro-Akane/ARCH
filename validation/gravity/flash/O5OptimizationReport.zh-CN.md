# O0–O5 优化实施与 ARCH–FLASH 4.8 对照记录

2026-09-24，`physics/selfgravity`。本记录对应
[实施计划](../../../docs/development/FlashComparisonOptimizationPlan.zh-CN.md)。
**结论：共享数学优化、CUDA 正向加速和现有四模块耦合已通过本机复验；
O5 的完整跨软件、等误差签收尚未完成。** 这两款程序的强燃烧方程、时间分裂和
动态 AMR 决策并不相同，不能用墙钟比值单独宣称算法优劣。

## 实施内容与正确性边界

- O0：把 Roe/Glaister 交叉状态、PPM 重构面和高阶面通量作为可拒绝的候选；
  回退后仍对实际采用的守恒状态和 EOS 查询保持严格检查。CUDA 只在候选查询时
  解除该候选的错误锁存，真实失败仍由原有状态通道报告。Host 只捕获表 EOS 的
  `runtime_error`，不吞掉配置与程序错误。原强 Cellular 算例 CPU/CUDA 均能推进，
  零下限修复；必需 EOS 失败测试保留。
- O2：Helm 一次反解同时提供端点压强和声速；燃烧在同一试算状态合并热容、
  组成能量梯度及 Jacobian 所需导数，保留原数值公式和严格表域。
- O3：aprox13/19/21 复用已有解析核素 Jacobian，冻结屏蔽的导数含义与原 AD
  比较；iso7 保持通用 AD。`screen5` 在一次速率求值内复用相同
  `(T,rho,abar,zbar,z2bar)` 的公共因子。
- O4：CUDA 状态区域改用按 `stride_y` 的二维异步拷贝合并行段；原有
  AMR、ghost、重启和存储所有权不变。弱 Cellular 探针的 memcpy 调用由
  22,898 降至 2,498，其中新路径有 2,000 次二维异步拷贝。这只是调用数，
  不是整程序加速声明。共享 EOS、网络和 ODE 数学没有另建 CUDA 版本。

本次没有回滚 Helm 严格逆解、修改容差、关闭守恒限制、改写测试真值、启用
`fast-math` 或降为单精度。`SNIaCoupled` 仍是厘米尺度 C/O 热点：FLASH 归档
没有对应的完整白矮星 SN Ia 初值及火焰模型；为制造同名对照而改成“整星”
会产生没有物理依据的算例。保留其四模块验收身份。

## 公共模型及物理口径

本机：Intel i7-10700（8 物理核/16 逻辑线程）、RTX 3060 Ti 8 GiB，
ARCH Release FP64/CUDA 12.3；FLASH 4.8 来自用户提供的源码归档，
本轮未改 Fortran 物理或数值源码，仅在独立解包目录把链接库指向本机 MA28。
归档的 `Cellular/Simulation_initBlock.F90` 本身带有口袋混合/点火模式扩展，
所以本报告的 FLASH 对象是**用户归档中的配置**，并非声称使用上游未改版。
受测输入设 `pocket_mode=4`、`pocket_vol_frac_he=1`、`ignition_mode=1`、
`noiseAmplitude=0`，即纯氦平面初值；初始有效场还另行对齐检查。
计时运行串行进行，使用 `/usr/bin/time` 的总墙钟，包含初始化和输出；
ARCH CPU 用 16 个 OpenMP 线程，CUDA 用 1 个主机线程，FLASH 用 1 个 MPI 进程。
这些资源**不相等**，下表比值是本机完成任务的成本，不能称为同核效率。

| 算例与资源 | 三次总耗时（秒） | 中位数（秒） | 物理终点/接受步 |
| --- | --- | ---: | --- |
| 归档 FLASH Cellular 2D，MPI1 | 4.41、4.56、6.73 | 4.56 | `1.0187727288607583e-8 s`，63 步 |
| ARCH Cellular 2D，CPU16 | 158.97、159.49、159.98 | 159.49 | 同一终点，138 步 |
| ARCH Cellular 2D，CUDA1 | 78.02、79.64、79.84 | 79.64 | 同一终点，138 步 |
| FLASH Sod 1D，MPI1 | 0.40、0.40、0.39 | 0.40 | `0.20037546896420744`，303 步 |
| ARCH Sod 1D，CPU1 | 0.22、0.23、0.23 | 0.23 | 同一终点，562 步 |

Cellular 的总耗时比为 ARCH CPU16/FLASH MPI1 **34.98**、ARCH
CPU16/CUDA1 **2.00**、ARCH CUDA1/FLASH MPI1 **17.46**。最终源码重建后的
完整复跑分别为 CPU16 160.55 秒、CUDA1 77.98 秒，终点和 138 步一致；
AMR 拓扑一致，密度/压强/温度的 CPU/CUDA 相对最大差低于 `1e-14`，
`ENUC` 低于 `4e-13`，两端修复事件均为零。此前三次正式计时与最终复跑
之间只收窄了 Host 候选 EOS 的异常类型，Helm 正常路径未变。

两端使用 64×32 cm、最细 `dx=0.5 cm`。FLASH 块有 8×8 活跃单元，ARCH
有 16×16；初始有效单元都为 3,584，终点都为 5,120。
按各自日志积分，FLASH 约 274,944、ARCH 约 651,264 个“活跃单元×宏步”，
后者为前者的 2.37 倍。FLASH 在第 20、42 步增至 68、80 个叶块；ARCH
在第 36 步由 14 增至 20 个叶块。因此相同最细网格和首末活跃单元数
并不代表相同 AMR 时空工作量。

同一物理终点的归档 FLASH/ARCH 体积加权相对 L1：密度 1.57%、压强 2.07%、
温度 3.16%、x 速度 2.64%；初始压强已差 0.212%。归因检查发现归档
FLASH 参数为 `eos_coulombMult=0`，而 ARCH Helm 保留 Coulomb 项。
在**不改 FLASH 源码**、仅设 `eos_coulombMult=1` 的单独控制组，初始压强
相对 L1 降至 `1.99e-6`；按照该组 FLASH 的实际终点
`1.0192286463896491e-8 s` 重跑 ARCH 后，终点密度/压强/温度分别相差
1.59%/1.98%/3.22%。初值 EOS 偏差因此已定位，后续差异仍包含
不同 Hydro、烧核耦合与 AMR 轨迹，尚无双方等误差结论。

关闭两端燃烧的诊断组：FLASH 63 步、3.29 秒，ARCH CPU16 120 步、
28.42 秒，均到 FLASH 的实际终点 `1.0188072904401767e-8 s`，总耗时比
**8.64**。ARCH 累计约 589,824 个活跃单元×步，约为 FLASH 的 2.15 倍；
终点密度相对 L1 差 1.10%。这个控制组只隔离燃烧对**各自完整运行**的影响，
不能把两个总耗时差相减解释成严格独立的 burner 计时。
FLASH burner 在一次调用内冻结温度/密度且每宏步调用一次；ARCH 对温度和
组分作第一定律自洽耦合，并在每步前后各做一次 burn 半步。
这些额外物理与工作量构成剩余成本的重要部分，仍需要等误差研究才能评价效率。

Sod 两端都是 256 个有效单元、理想气体 `gamma=1.4`、HLLC/MC、
全 outflow 边界。ARCH 对**独立单元平均解析 Riemann 解**的密度 L1 为
`0.0022172`、压强 L1 为 `0.0017587`；两端直接对齐的密度、压强相对 L1
分别为 0.142%、0.142%。此小型纯 Hydro 算例 ARCH CPU1 中位总耗时
为 FLASH 的 0.58 倍，不能外推到 Helm/烧核性能。

## 完整能力线：自引力、燃烧、热扩散、Hydro

| ARCH 原有算例 | 接受步/终点叶块 | CPU16/CUDA1 总耗时（秒） | CUDA 加速 | 最差 Poisson 残差/目标 |
| --- | --- | --- | ---: | ---: |
| `SNIaCoupled_2d_cartesian_amr.par` | 5/13 | 12.37/4.80 | 2.58 | CPU 0.268、CUDA 0.268 |
| `SNIaCoupled_3d_cylindrical_amr.par` | 5/9 | 99.65/58.22 | 1.71 | CPU 0.462、CUDA 0.462 |

两组 CPU/CUDA 终点层级、Morton 号和坐标相同；密度、压强、温度、
组分、势与引力加速度的差异为浮点舍入量级，`ENUC` 的相对最大差
不超过 `1.4e-12`。每组两端均有 17 次合格 Poisson 解、零状态修复；
非零 `GPOT/GACX`、反应放能及 C12 消耗可见，`dt_diff` 有限。
二维周期域的体积加权质量相对漂移为 `1.9e-16`。
三维柱坐标算例有 outflow 边界，域内质量变化约 `-0.72%`；
本轮没有独立边界通量账本，不能把它直接归因于 AMR 或称作严格质量守恒。
两组只运行 5 步，是耦合执行与后端验收，尚不是整星 SN Ia 演化验证。
`SNIaCoupled` 的 Setup 要求四模块同时启用，不能靠关掉一个开关做同一算例的
消融；本轮没有独立量化该算例中导热对终态的贡献。

独立复核：现有 Helm、aprox/NSE、单区燃烧主轨迹、tabular 严格域与
3D/4D 组件测试通过；CUDA 的 hydro EOS 失败、AMR exchange/regrid、
多块 Hydro、网络设备和生命周期六项回归通过。平面 Sedov 的独立相似解
128/256/512 单元三组、Jeans/Poisson 快速检查 33 项通过；高斯热扩散
“开/关”控制组在 CPU/CUDA 分别给出 `1.0579768%` 的可分辨能量变化。
Sedov 和热扩散是**focused gate**，其记录明确标注 `release_qualified=false`；
本报告不把它们扩大解释成完整发布签收。

## 尚未关闭的签收项

1. Tabular3D/4D 的 burn ODE 拒步和 NSE 线搜索可能先把**可恢复试探**
   写入整个设备批次的 sticky EOS 状态。本轮验证了必需失败不能假成功，
   但没有真实表 EOS 的“试探失败后成功接受”成对轨迹。因此 C5 仍是
   风险条目；不能清除全局错误位来制造通过，需先构造 Host/Device 同一
   轨迹，再设计局部候选结果与严格最终检查。
2. 本机 FLASH 归档的 `Sedov/Simulation_initBlock.F90` 已被改为随机生成
   25 个爆点和随机压强倍率，原始运行不能与 ARCH 单爆点 Sedov 对齐。
   原 Fortran 源码未被改写；这份 FLASH Sedov 不纳入性能表。
3. FLASH ConductionDelta、Jeans、统一 EOS 样本和完整四模块的双方
   **等方程、等误差**数据尚未建成。对后两者，尤其是 SN Ia，没有
   可直接复用的归档 FLASH 初值组合。本报告给出受控模型和 ARCH
   独立验收，不宣称 O5 所设六类公共模型矩阵全部通过。
4. O2/O3 的反解、插值、ODE RHS/Jacobian、分解次数尚无跨后端统一
   生产计数；O4 的小拷贝探针不代表大算例总耗时收益。后续 H100
   必须复用相同输入、物理终点、工作量与误差口径，再补 GPU 资源证据。

因此本轮可以合并**已验证的 O0–O4 改进和 O5 已执行的性能/科学记录**，
但不应把“ARCH CPU 已达到 FLASH 同资源、等误差效率”或“完整 SN Ia
跨软件对比完成”写为能力声明。

## 复现入口

- ARCH 的可保存示例输入：
  [CellularFlash2D.par](../../../simulation/Cellular/CellularFlash2D.par)，
  [SodFlash1D.par](../../../simulation/Sod/SodFlash1D.par)，
  [SNIaCoupled](../../../simulation/SNIaCoupled/README.md)。
  FLASH 使用用户归档中的 `Cellular`、`Sod` 构建；受测 `Cellular`
  setup 为 `setup.py Cellular -auto -2d -maxblocks=15000`，`Sod` 为
  `setup.py Sod -auto -1d -maxblocks=64 -nxb=16`。归档的 `Cellular`
  已有上述初值扩展，本轮未改 Fortran；Coulomb 对齐组仅在复制的
  `flash.par` 中将 `eos_coulombMult` 从 0 改为 1。
- 场比较使用现有 [HDF5 比较器](compare_cellular.py)，读取双方**实际**
  终点时间、叶块和 CGS 场；原比较器输出字段名 `step_20` 是历史名称，
  本报告的长时数据实际为 138/63 步，不把该键解释成步数。
- 本机原始输出和输入保存在 `/tmp/arch-cellular-extended-20260924/`、
  `/tmp/arch-final-source-long-20260924/`、`/tmp/arch-o5-20260924/`、
  `/tmp/arch-sod-20260924/`、`/tmp/arch-flash-sod-20260924/`；这些
  临时路径是本机审计证据，不是 CI 或未来 H100 的文件依赖。
