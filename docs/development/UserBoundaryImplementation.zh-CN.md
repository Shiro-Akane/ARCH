# 用户边界实现与验收

本文面向维护者，记录统一用户边界的实现范围、科学判据与交付证据。
用户入口为[边界指南](../guides/UserBoundaries.zh-CN.md)，阶段要求见
[计算优化计划](ComputeOptimizationPlan.zh-CN.md#4-o8统一用户-bc-接口物理含义分别表达)。

实现分支为 `codex/o8-boundaries`，代码起点为 `d96e929903da4e327ac79f53dde60018bdf8593c`。
结果以实际可执行文件摘要和冻结输入为身份；文档起点提交不能代替运行文件身份。
原始 HDF5、checkpoint、编译缓存与设备诊断输出留在本机，提交仅包含处理后的摘要。
处理后的[验收记录与复现入口](../../validation/gravity/results/user-boundaries-20261003/README.md)
集中保存实际身份、完整覆盖映射及成功／负收益结果。

## 接口与所有权

| 内容 | 数学／规则所有者 | 执行适配 |
| --- | --- | --- |
| 回调注册与算例身份 | `physics/boundary/UserBoundary.h`、既有 ProblemRegistry | CMake 收集同目录 `.cpp` 并写入各自源码摘要 |
| 物理面、法向与坐标接合 | `grid/CoordinateBoundary.h`、`HostBoundaryPlan.h`、既有 CoordinateSeamPlan | Host 计划与 CUDA donor/ghost 槽位绑定 |
| primitive／扩散控制验证 | `PhysicalBoundaryHandler`、`PhysicalBoundary.h`、既有 EOS | Host 回调求值；共享状态和通量数学 |
| 实际面通量及收支 | `BoundaryFlux.h`、`BoundaryDiagnostics.h`、`DriverBoundaryDiagnostics.cpp` | Host 累计与 CUDA 表面观察面 |
| 引力条件、算子失效与 RHS | `GravityUserBoundary`、`CompositePoisson`、`GravityWorkspace` | 同一 CompositeMultigrid／FGMRES 与 Host/CUDA execution |
| 阶段时间 | 既有 StageScheduler、RK 与 RKL 系数 | Driver 传递真实输入阶段时刻 |
| 区域同步 | 既有 ComputeBackend／StateResidencyLedger 事务 | 已同步内域保留，仅刷新需要传输的 ghost；失败不发布完成 |
| Host 表面并行 | `PhysicalBoundaryHandler::apply_device`、既有 HostFailure | 只读快照、独立样本／面行；循环完成和异常核对后才按固定顺序 scatter |
| 重启身份 | 既有 HDF5Writer／CheckpointCompatibility | 格式 7 的可移植边界身份 |

用户只需 `<UserInterface.h>` 与 `<GlobalDefs.h>`。流体／扩散使用固定的
`physical_boundary.cpp`，自引力使用 `gravity_boundary.cpp`，与算例源码同目录。
两套上下文、返回类型和注册宏彼此独立；函数式与类式注册汇入同一描述。
选择 `user` 后缺少对应注册、错文件名、错目录、重复注册或缺源码摘要均明确报错。
这些文件随程序编译；启动阶段进行身份解析，而非按用户路径加载动态代码。

## 物理与数学范围

物理边界支持既有周期、外推和反射；`neumann` 是齐次外推别名，指定入流／
`dirichlet` 使用用户 primitive 接口。扩散单独表达指定值、法向梯度及外法向通量。
热、原生速度三分量与全部核素通道按启用物性验证。组分扩散总质量流须为零，黏性
机械功进入既有能量通量。无有效 EOS 状态或缺少物性闭合时明确失败。

引力沿用周期、孤立及正式 composite-AMR 路线，补充齐次 Dirichlet、Neumann 和
逐面用户条件。线性 Robin 写为 `a Phi + b dPhi/dn = c`，要求有限 `a>=0,b>0`；
指定势使用独立 Dirichlet 构造函数。周期方向成对，类型和 Robin 系数在每一阶段的
同一侧保持一致；阶段间变化重建算子，只有 `c` 变化时更新 RHS。

纯 Neumann 使用物理面积／体积检查 Gauss 相容性，固定零体积平均势。正质量与
全零 Neumann 的不相容问题明确拒绝。周期问题的背景扣除仍属于原有周期模型。
流体反射、轴线正则、引力镜像质量各有物理含义；回调本身不建立域外质量模型。

三类当前原生几何均可使用：Cartesian 的 x／xy／xyz，柱坐标 r／(r,phi)／
(r,z,phi)，球坐标 r／赤道 (r,phi)／(r,theta,phi)。有效环域、扇区和楔域
可指定势条件；孤立质量闭合保持已有完整方位要求。二维 RZ 的语义迁移属于另一
工作线，本分支没有替换当前二维柱坐标模型。

内部 AMR 接口不调用物理回调。零面积轴线／极点由坐标接合拥有；正的极小内半径
仍属于真实物理边界。角点采用 x1→x2→x3 的固定优先级，最后活动轴拥有交角 ghost，
两后端读取同一阶段内侧快照。RK／RKL 使用真实阶段时间；RKL 热收支使用其递推系数。

## 双精度通量修正

拟合的指定势面导数写为 `g=Q Phi+qB PhiB`，下／上侧外法向符号为 `s=-1/+1`。
令 `D=a+b*s*qB`，Robin 消元得到：

$$
g=\frac{aQ\Phi+q_Bc}{D}
 =\sum_i\frac{aQ_i}{D}(\Phi_i-\Phi_A)
  +\frac{q_Bc}{D}-\frac{a q_B\Phi_A}{D}.
$$

指定通量项直接累计，避免先计算 `c-PhiA` 再补回势值。在 `PhiA=2^54,c=1`
的 Neumann 反例中，旧表达式返回 0，独立物理导数为 1；零体积平均规范并不能
恢复已丢失的低位。近 Neumann Robin 同时保留真实 anchor 权重。零 stencil 权重
不读取无关的场差，避免两个有限极值相减溢出后出现 `0*inf`。

Host 标量、Host 打包执行及 CUDA kernel 调用同一数学函数。原指定势／周期面保留
求值顺序。已有复合泊松与 CUDA execution 检查扩充上述反例，原解析误差、残差和
守恒门槛保持不变。近 Neumann 局部检查用另一侧 Dirichlet 固定全域规范：它验证
通量公式，不能作为任意病态弱 Robin 系统都可在双精度下求解的证据。无法分解或
不能满足残差的系统仍明确失败。

## 设备与收支

普通回调在 Host 执行。CUDA gather/scatter 只交换所需 donor／ghost 切片及控制面，
不用全域 Host 回退。拓扑拥有缓存和显存；新增标记仅在层级实际含 flux 边界时上传。
内置周期／指定势路径不建立用户回调的切片、观察面或额外回调同步。

真实设备场景补齐了两项运行问题。仅刷新 ghost 的回调会留下“内域已同步、ghost
仅设备有效”的合法状态；同步事务现在只传输未同步的区域，保留内域完成标记，
仍拒绝陈旧、未完成及错误方向的源。既有后端契约检查覆盖 H2D/D2H 和 fence 失败。
CUDA Hydro 重构从左单元起步，通量却按右单元面索引保存；观察器现在使用同一面
索引，正确捕获两端。已有真实流体 route／integrator 检查增加体积守恒量与实际
边界通量的闭合判据，该判据在修正前失败。修正观察器前后演化通量公式保持一致。

`boundary_fluxes.tsv` 按实际 RK／RKL 系数累计质量、原生动量、流体能量、核素和
热通量。`gravity_boundary_exchange.tsv` 记录相邻发布势场的 `0.5∫rho Phi dV`
和 Green 交换。含阶段发布的时间序列可回转；它不是宏步全系统能量守恒证明。
曲线坐标原生动量也不能直接作为全域笛卡尔动量守恒量。诊断在重启后重新累计。

Checkpoint 保存算例、回调源码摘要及科学自定义输入身份；不依赖机器绝对路径。
CPU↔CUDA 可移植续算保留原物理身份，源文件或边界输入改变明确拒绝。

## 验收记录

2026-10-03 在 WSL Ubuntu、RTX 3060 Ti、CUDA 12.3 上验收；CPU 优先，随后 CUDA。
CPU 为 GCC 13 Release，CUDA 构建的 Host 编译器为 GCC 12。共享数学保持严格浮点；
正式 CPU/GPU 成本用同一 CUDA-enabled Release 程序分发到两后端。

| 入口 | 已取得结果 |
| --- | --- |
| 既有 CPU CTest | 61/61，58.88 s；含四线程表面逐位对照及失败不发布检查 |
| `user_boundaries.py --backend cpu` | 24 项；三几何／维数、轴极点、RKL1/2、AMR、收支、续算和拒绝路径 |
| 线性时间热通量独立积分 | `q=q0*(1+t/(1 s))`，最终误差约 `2.54e-21` erg |
| CUDA CTest | 收敛后 125/125，180.26 s；完整 inventory／JUnit 无缺项与跳过 |
| 工具检查 | 架构审查通过；359/359，约 18.17 s，无跳过 |
| 设备边界／跨后端续算 | 45 项记录，CUDA Host8；含三几何／维数、独立收支、九组场对照、双向重启与拒绝 |
| 既有引力／四模块耦合 | 42 项记录；周期／孤立、混合 AMR、燃烧／扩散、续算和非收敛拒绝 |
| Compute Sanitizer | Host8 的 20 次 memcheck 零错误／泄漏，14 次 racecheck 零报告 hazard；区域生命周期另有一次 memcheck |
| 同终点成本 | 各五组交替；内置整程 1.247 倍、纯 CPU 新／旧 0.985；短回调整程 0.823 倍 |

科学验证使用 `work` 环境。其 Python 3.12 构建不含 Linux pidfd 接口，资源保护
工具检查采用系统 Python 3.12 并读取同一 `work` NumPy／h5py；没有更改检查条件。
racecheck 覆盖所运行 kernel 的工具可识别 hazard，不能据此证明任意 global-memory
并发访问都无竞态。原始插桩日志仅留本机。

固定构建与检查入口如下，输出目录须为空；原始输出留本机。

```bash
cmake --build build-cpu --parallel 3
ctest --test-dir build-cpu --no-tests=error --output-on-failure -j 2
cmake --build build-cuda --parallel 3
ctest --test-dir build-cuda --no-tests=error --output-on-failure -j 2
python validation/gravity/user_boundaries.py --arch bin/ARCH \
  --backend cpu --threads 1 --output output/user-boundary-cpu
python validation/gravity/user_boundaries.py --arch build-cuda/bin/ARCH \
  --backend cuda --threads 1 --reference-arch bin/ARCH \
  --output output/user-boundary-cuda
python validation/gravity/check_cuda_compatibility.py \
  --cpu-arch bin/ARCH --cuda-arch build-cuda/bin/ARCH \
  --output output/gravity-cuda-qualification
python validation/gravity/user_boundaries.py --arch build-cuda/bin/ARCH \
  --backend cuda --cuda-sanitizer /usr/local/cuda/bin/compute-sanitizer \
  --sanitizer-tool memcheck --output output/user-boundary-memcheck
```

本阶段扩充既有边界／Poisson／执行检查，不新增工作流矩阵或 FLASH 依赖。
既有 CI 触发条件保持。正式成本、完整边界运行与 sanitizer 属于手动
验收，短回归继续用现有 CTest 入口。长时科学验证按冻结阶段另行实施。

计时继续使用 `check_cuda_compatibility.py --benchmark-only`，以
`--benchmark-time` 明确同一物理终点，取消按宏步数截停的计时方式。每条轨迹验证
终点、状态、残差和修补计数，预热后交替运行至少三次，默认五次。CPU/GPU 比值
来自同一个 CUDA-enabled 程序；`--baseline-cpu-arch` 可额外记录同编译器的旧版与
当前纯 CPU 程序比值，二者的分母分别报告。设备内存／温度／功耗采样不等于进程
独占使用量。编译、sanitizer 与正式计时分开执行。

本轮周期 64³ 比较冻结共同终点 `t=0.02 s`；用户回调 64³ 比较使用
`t=0.0003 s`，两者是不同场景，分别报告。此前 `t=0.1 s` 的单线程预热
包含 72 步、约 320.6 s Driver 时间，仅作为预热成本记录；中止的后续预热不计入
正式统计。两条正式路线均无步数截停，同一场景的输入、CFL、输出和终点保持一致。

## 运行组织与性能回查

同一状态版本、AMR epoch、实际阶段时间和用途下，确定性回调可复用已完成的 ghost。
时间按双精度位模式比较，Hydro／Diffusion 用途分别标识；新的状态或拓扑仍由既有
residency ledger 触发刷新。只有完整边界、交换及发布成功后登记复用身份。
该变化不减少独立的物理时间阶段，也不许可回调依赖调用次数。

区域传输改为内部单块和其 ghost 补集的六个互斥三维块；每个场和核素 plane
保留原字节范围、Host species stride 和显存布局。三维拷贝的基指针与位置只应用
一次偏移，slice pitch 为完整平面的行数乘 row pitch。旧逐层矩形 helper 已移除。
独立主审使用实际旧／新 helper 验证 1,500 种布局、3,000 个区域，包含空范围与
padding；设备生命周期检查逐位验证一至三维、两核素、正负零及未传区域哨兵。

普通回调的每个 Host 表面样本拥有独立输出与唯一的 `(face,plane)` 控制行。
较大的表面批次使用既有 OpenMP 线程，嵌套并行调用和小批次保持串行；没有跨样本
归约或数学次序变化。异常由既有 HostFailure 捕获，全部线程完成并检查失败后才
按原 x1→x2→x3 顺序 scatter。CUDA gather／scatter 和所有 runtime 调用位于并行
循环之外。既有 `compute_backend` 入口补测三维两用途的串／并行逐位一致性、四线程
实际执行、所有面控制行及注入异常时零发布；没有增加 CTest 目标。

正式计时显式记录 `--benchmark-cuda-host-threads`。原 CUDA Host1 数据保留，
Host8 数据与 CPU8 按相同 Host 资源比较；其中的 Host 并行收益不能称为 kernel 提速。

只有包含 Neumann／Robin 面的层级上传 flux 标记和 anchor 权重。无此边界的层级
在循环外选择 legacy work，以常量参数调用同一 `composite_face_gradient`；原
CompensatedSum、包括零项的求值顺序与系数保持。Host 与 CUDA 共用此分发描述。

首次完整计时中，周期内置路径的纯 CPU 比值为 1.051，随后独立三对复测为 1.079；
用户回调 CPU/GPU 整程比为 0.435，均作为待修正问题保留。CUDA API 诊断显示
254,464 次 `cudaMemcpy2DAsync` 提交耗时约 7.681 s，逐 z 层传输是明确开销来源。
此 WSL profiling 未采集 kernel 时间，API 比例不能当作 kernel 性能或 FP64 归因。
同一冻结输入和物理终点下的最终复测另列；没有删减输出、边界求值规则或物理步骤。

周期内置边界最终五组交替测量中，同一 CUDA-enabled 程序的 CPU8／CUDA
整程中位数为 22.960／18.420 s，加速 1.247 倍；范围为 22.562–23.692 s 和
18.336–18.628 s。GCC 13 纯 CPU 新／发布版中位数为 22.459／22.793 s，
比值 0.985，范围为 22.294–22.946 s 和 22.103–23.254 s。
两后端均完成 15 步至 `t=0.02 s`；没有持续超过 5% 的内置路径回退。
此前各次测量的环境波动和源码身份保留，不能直接将跨轮墙钟差都归于某个优化。

用户回调同终点五组复测：精确 cuboid 传输后，CPU8 为 8.665 s、CUDA Host1 为
12.316 s；表面并行后，CPU8 为 8.911 s、CUDA Host8 为 10.833 s，整程加速比
为 0.823，仍低于 1。后者范围分别为 8.885–10.033 s 与 10.565–11.436 s，
泊松部分为 2.302／1.521 s（1.514 倍）。两种后端都完成 3 步至 `t=0.0003 s`，
每步 checkpoint 的输出配置保留。分别测得的输出中位数为 0.327／1.131 s。
这条短回调轨迹需要 Host 求值及设备表面往返，不能承诺整程正收益。

同配置 CUDA API 再诊断中，区域拷贝提交改为 15,232 次 `cudaMemcpy3DAsync`，
约 0.630 s；普通 `cudaMemcpyAsync` 约 2.320 s。API 提交成本明显下降，
而这些统计既不包含全部 Host 工作，也不提供本机缺失的 kernel 时间线。
表面求值并行前后的整程差异另外测量；由 API 数据不能精确分摊其全部来源。

## 测试入口收敛

CUDA 燃烧策略的 12 条网络／ODE 路线与 3 条控制器状态检查归入四个网络矩阵和
一个状态矩阵；独立 helper、设备执行、controller、导数、EOS 与参考解检查仍各有
责任。单路线 CLI 保留，矩阵失败逐条定位并继续其他子项。每个子项仍创建独立
CUDA 上下文，数学函数、固定参考、ULP／误差预算和 table 指纹逐字保留。

既有 CUDA 总清单从 135 项收敛至 125 项。合并前后整程为约 184.45 s 和 184.68 s，
没有可证明的整程提速；主要收益是减少碎片入口，避免后续为每条组合重复建目标。
实际 CUDA 初始化、EOS table 读取和大批量数值工作未省略。完整清单仍须通过。
最终增量构建后全套为 180.26 s；这约 2% 的单次差异不作为稳定提速结论。

该有界 DeepSeek CLI 任务采用 Flash high，147.73 s，交付后由主智能体独立核对
前缀、分发和覆盖映射，并完成 NVCC／125 项回归；无需 worker 修复。
报告 token 为输入 1,010,484（缓存 978,432、非缓存 32,052），输出 32,965；
推算费用 $0.02752–$0.05504，实际账单与主智能体成本未知。等待期间并行完成真实
设备／跨后端运行与文档工作；重叠秒数和主智能体 review 耗时未单独测量。
它是用户请求的测试组织任务，与下方独立数学验证试行分别记录。

其余三个有界实现任务分别处理 legacy 分发、精确区域传输和 Host 表面并行，
均通过范围复核与对应实际运行。区域传输初稿重复应用了三维起点偏移，主审在合入前
发现，并在一次正式修复后验收；错误初稿未进入生产源。新区域测试曾把 padding 当作
有效逻辑单元初始化，主审修正了测试足迹，保留全部哨兵和误差门槛。
各任务的累计 token／估算费用、修复及未知等待时间见[委派摘要](../../validation/gravity/results/user-boundaries-20261003/delegation.json)。

## 条件独立验证试行

单次 DeepSeek 任务仅核对冻结消元公式及极端反例，采用无本地工具的 API 文本任务。
实际调用 Flash high，HTTP 200，14.76 s；1960 输入 token（缓存 128）、3072 输出
token。输出全部用于推理，达到上限后没有最终交付；worker 验证未通过，未重试。
API token 推算费用约 $0.00212–$0.00424，实际账单及主智能体费用未知。

本轮有效缺陷由主智能体独立构造、复现并修正，不能归于 worker。没有完整交付，
重复和误报无法评定。唯一有时间戳的独立活动与 API 的实测重叠为 0 s；其他重叠、
阻塞等待和主智能体核对／返工耗时未独立测量。一次必要的算法修正不属于 worker
误报返工。建议保留条件试行，但调整任务粒度或推理输出预算；本次没有普遍提速证据。
