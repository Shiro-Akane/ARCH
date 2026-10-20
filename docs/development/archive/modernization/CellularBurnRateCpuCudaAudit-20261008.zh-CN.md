# Cellular 燃烧能量释放率的 CPU CUDA 差异报告

日期：2026 年 10 月 8 日。供求解器维护者审阅；本分支只提交本报告，不提交源码补丁、GNN 实现、参数文件或原始 HDF5。

## 问题概述

流体本身没有出现明显异常，但“核反应正在以多快的速度释放能量”这一数值，在 CPU 和 CUDA 上没有达到本轮沿用的比较精度。原版 `ee091b35c` 在相同初始场、相同物理时间的第 1 步就能复现；关闭 GNN 记录器、不使用新收支观察器，差异仍然存在。

已经暂停扩大训练数据采集和训练放行。现在确认的是**验收判据未通过**，不是已经确认求解器有 bug。需要区分反应积分的误差传播、后端计算差异和速率字段本身的合理误差预算；本次没有修改数学实现，也没有把容差放宽到结果上方。

## 报告分支与复现基线不是同一个提交

本报告分支从朋友仓库当前 `main` 的 `25adec4224497981a0c124a3485f786194975be4` 创建，只新增文档。**请不要直接用报告分支的源码当作已执行的复现版本。**

实际复现使用 [ee091b35c 数值修复提交](https://github.com/Shiro-Akane/ARCH/commit/ee091b35c45e959910a6a4520aa240856c0d560d)。它属于 `codex/o8-boundaries` 的历史；本次核对该分支远端头为 `11a321d5604f9ee62b9f9587c81f14de4f128bc4`。本文不宣称后来或其他提交也已经执行过同一验证。

先在 `ad3173bcf43868a95029e737fa99e61db5169cb8` 的记录器关闭状态发现差异，再退回上述原版二进制复现。对两端各自的终点做比较后，原版与关闭记录器版仅有增加输出频率导致的 `/@chk_index` 差异，其余物理场、时钟、拓扑、身份和修正账本逐位相同。严格全字段比较的原始失败仍保留，没有改写为全字段通过。

## 测到了什么

主检查使用场峰值归一化的差异，沿用 `rtol=5e-9`、`atol=5e-12`。纯相对诊断 `5e-11` 单独保留。这些是本轮事前登记的筛查设置，尚不是已证明适合刚性核反应速率的专门误差上界。

| 检查 | 能量释放率差异除以字段峰值 | 时间条件 |
| --- | ---: | --- |
| 第 1 步 | 1.19544936e-6 | 两端精确 t=1e-16 |
| 第 2 步 | 2.52599190e-5 | 同步数诊断最大值；时间相差约 4.7e-31 |
| 第 24 步 | 5.96939017e-7 | 两端精确 t=2.0739585970773817e-15 |

第 2 步不是精确同时间结果；第 1、24 步的原生精确时间比较均返回失败，足以确认这不只是把不同时间拿来比较。

首步最大差异位于层级 5、逻辑块 `(55,0,0)`、块内物理单元索引 `15`（从零计数），在本一维域中的中心为 `x=6.99609375`。原生比较器给出的扁平索引是 `671`。关联依据是固定域下的层级、逻辑坐标和物理时间，不是运行时 UID。

该点首步的 `enuc_rate` 为：

- CPU：`9.164524775027884e26`，十六进制 `0x1.7b092e2d5d61ep+89`。
- CUDA：`9.164513819302608e26`，十六进制 `0x1.7b09107b4da14p+89`。
- 绝对差：`1.0955725275888123e21`。差异约为峰值的百万分之一，并非模拟崩溃或数量级失配，但超过当前判据。

逐步输出覆盖第 0 至 24 步，共 25 对 checkpoint。初始场完全相同，24 个非零步只有 `enuc_rate` 超出当前字段筛查。其余场在这段中的最大峰值归一化差异为：

| 量 | 最大差异 |
| --- | ---: |
| 密度 | 0 |
| 总能量密度 | 4.3372e-14 |
| 组分质量分数 | 9.3536e-14 |
| 组分质量密度 | 9.5382e-14 |
| x 动量 | 3.1334e-17 |
| y、z 动量 | 0 |

逻辑拓扑、身份字段和 48 项修正账本在这 25 对中一致。没有发现非法密度、内能或组分；但这不代替独立能量收支或长期精度验收。

燃烧候选限制步长 `dt_burn` 的最大相对差异约为 `2.526e-5`。它是候选限制值，不等于实际采用的宏步长也改变了这个比例；宏步长还有增长因子等约束。是否影响更长演化仍待核查。

无燃烧热扩散对照的 24 步同步数场比较通过，最大场尺度化差异约 `2.5344e-15`；它只帮助缩小问题范围，不代表完整 CUDA 或精确时间验收完成。

## 已排除和仍不能下结论的部分

本轮新收支观察器的 CUDA 构建在问题复现后主动停止，尚未生成用于验收的可执行文件，更没有参与上述复现。停止的是本任务所属构建守护进程，没有停止其他 GPU 工作。构建日志中的非零退出为主动终止的 `143`，不是已定位到编译器故障。

因此，现有证据不支持把差异归因于新观察器或 GNN 接入，也不是此前累计修正账本的 1 ULP 差异。

[共享能量交接逻辑](https://github.com/Shiro-Akane/ARCH/blob/ee091b35c45e959910a6a4520aa240856c0d560d/src/driver/stages/DriverBurnPolicy.h)用已接受的反应能量积分 `energy_change` 除以燃烧半步时长得到 `enuc_rate`，并用相同增量计算候选步长。CPU、CUDA 都调用这套交接实现。它不是用两个大 EOS 能量直接相减推测热释放，不能未经证据就将这次差异归因于那种相消误差或原子累加顺序。

当前仍未定位到第一个发生分歧的燃烧半步、ODE 子步或积分内部量；也未确认是积分误差预算问题还是实现缺陷。第 1 步 checkpoint 是目前最短的已确认时间定位，不冒充已经定位到了内部阶段根因。

## 如何复现

请在单独的服务器工作目录使用 `ee091b35c45e959910a6a4520aa240856c0d560d`，不要在报告分支直接构建。环境为原 GCC 12.4、CUDA 12.3、H100-20C 20 GiB vGPU，Release；CUDA 架构为 90，保留原 `--fmad=false --ftz=false --prec-div=true --prec-sqrt=true` 等精度约定。没有切换求解器、反应网络、ODE 容差或 AMR 阈值。

执行配置为一维 CellularDet、Helmholtz EOS、aprox19、BD、DenseLU、RK2、MUSCL/HLLC 和恒星热扩散 RKL2。以下是实际复现 CPU 参数去掉注释后的内容，只把三项机器路径改成可替换路径。左 x 边界未显式设置，沿用该提交解析器的 outflow 默认值；不要额外改成 reflecting。`base_name=rt` 只是文件名兼容，不表示此算例是 RT。

```ini
geometry = cartesian
nblockx1 = 32
nblockx2 = 0
nblockx3 = 0
max_blocks = 20000
x1_min    = 0.0
x1_max    = 128.0
x2_min    = 0.0
x2_max    = 12.8
x3_min    = 0.0
x3_max    = 1.0
x1r_boundary_type = outflow
x2l_boundary_type = outflow
x2r_boundary_type = outflow
x3l_boundary_type = outflow
x3r_boundary_type = outflow
solver      = HLLC
reconstruct = muscl
time_integrator = RK2
limiter     = mc
cfl         = 0.8
EntropyFix = true
EntropyFixCoefficient = 0.1
sml_rho = 1e-2
max_eint = 1e19
lrefinemin = 0
lrefinemax = 5
regrid_interval = 2
refine_var = DENS
refine_threshold = 0.80
derefine_threshold = 0.20
tmax = 2.0739585970773817e-15
max_steps = 88
restart = false
out_dir = ./cellular-rate-cpu
base_name = rt
plt_dstep = -1
plt_dt = -1
plt_variables= ALL
chk_dstep = 1
chk_dt = -1
eos_type = helmholtz
eos_table_path = /ABSOLUTE/PATH/TO/helm_table.dat
gravity_type = none
use_burn = true
network_name     = aprox19
nuclearTempMin   = 1e9
nuclearDensMin   = 1e-10
dt_init          = 1e-16
dt_min           = 1e-20
tstep_change_factor = 1.2
enucDtFactor     = 0.05
ode_solver       = BD
linear_solver    = DenseLU
shock_dir = 0
radiusPerturb = 7.0
rhoAmbient = 1.0e7
tempAmbient = 190000000.0
rhoPerturb = 4.322e7
tempPerturb   = 4.670e9
velxPerturb   = 1.011e9
noiseAmplitude = 0.0
xhe4 = 0.4
xc12 = 0.6
xo16 = 0.0
use_diffusion = true
diff_integrator = RKL2
diff_cfl = 0.8
diff_max_stages = 1024
use_thermal_diff = true
use_viscous_diff = false
use_species_diff = false
compute_backend = cpu
log_dir = ./cellular-rate-cpu
```

将上面保存成 CPU 参数副本；再创建 CUDA 参数副本，只改 `compute_backend=cuda`，并将 `out_dir`、`log_dir` 改为另一个未使用的 CUDA 目录。两份都把 `eos_table_path` 指向同一张真实 Helmholtz 表。所有输出目录使用新目录，不覆盖旧证据。`max_steps=88` 是安全上限，本次两端均在目标时间、第 24 步结束。

在已加载项目既有环境的 shell 中，分别执行以下命令。占位的二进制路径应替换为原提交对应构建；不要在 GPU 忙碌时杀其他进程：

```bash
OMP_NUM_THREADS=1 OMP_DYNAMIC=FALSE OPENBLAS_NUM_THREADS=1 /path/to/cpu/ARCH CellularDet cpu.par
OMP_NUM_THREADS=1 OMP_DYNAMIC=FALSE OPENBLAS_NUM_THREADS=1 /path/to/cuda/ARCH CellularDet cuda.par
```

执行前清除当前 shell 中以 `ARCH_GNN_`、`ARCH_CONSERVATION_`、`ARCH_RT_REFLUX_` 开头的诊断变量。此次运行没有设置这些变量。

最短已保存比较输入是两端 `rt_chk_0001.h5`。用同一冻结提交的原生 checkpoint 比较器：

```bash
/path/to/arch_cuda_single_level_validation --compare-physical-time ./cellular-rate-cpu/rt_chk_0001.h5 ./cellular-rate-cuda/rt_chk_0001.h5 5e-9 5e-12 5e-9 5e-9 1e-16
```

预期返回码为 1，首个失败字段 `enuc_rate`、索引 `671`。终点比较则将文件改为 `rt_chk_0024.h5`，目标时间改为 `2.0739585970773817e-15`。如果只需更短的重新启动实验，可以另登记输出目录并将 `max_steps=1`；此次真正执行的是两端逐步输出到第 24 步，没有冒充另外执行过独立单步启动。

## 证据身份与保存位置

原生 CPU／CUDA 复现二进制 SHA256：

```text
CPU   7f90533c2aa480c3ab5adb3d89ac9595d458573038e618e01acdeb52181e0ace
CUDA  2e7902b7618c2df5446a675a82f88c216f885d0eda9ecb8fa1e8f91f8011c42a
EOS   c9a57c26c6fd2b2b378b9d5295ca1214022f6fec6289d038b47bf8c8938881a1
```

首步 checkpoint SHA256：

```text
CPU   6037bbc0fb197f7686a16e7a4742cf2dd2774afe74b02a4f00d0329b846d7421
CUDA  11e7602339857d05f128d173f417d9a388d3fd3b33cdd4e22267032cb179eb9f
```

完整原始记录保存在服务器 `/home/ubuntu/projects/ARCH-conservation-cuda-evidence-20261008`。其中 `frozen-reproduction/comparison.json` 包含逐步字段值、十六进制、逻辑位置和时钟；`release-hold.json` 集中列出验收暂停原因。

另已在用户实验仓库的固定提交 `4ee783a1f26a485d27070307e4c09e54396c8c0b` 保存[完整证据清单](https://github.com/Arsenic-er/ARCH-AMR-Experiments/blob/4ee783a1f26a485d27070307e4c09e54396c8c0b/experiments/predictive_amr/cellular_conservation_cuda_v1/evidence/manifest.json)和[结果摘要](https://github.com/Arsenic-er/ARCH-AMR-Experiments/blob/4ee783a1f26a485d27070307e4c09e54396c8c0b/experiments/predictive_amr/cellular_conservation_cuda_v1/evidence/result.json)。该仓库若需要访问权限，请向仓库所有者申请；本次不变更共享权限。

证据包 `cuda-burn-rate-hold.tar.gz` 为 587,599 字节、131 个文件、66 份 HDF5，逐成员哈希核对通过；SHA256 为 `536cd686d7a7ab952e2cb0e8983592fb51cc0eeb8682022fe3315a1d8b5bfe65`。本报告分支不复制该证据包或 HDF5。

## 建议审阅顺序与当前决定

1. 从首步异常逻辑单元开始，核对两个燃烧半步的输入状态、半步时长、接受的能量积分、BD 子步与误差估计，确认最早分歧发生在哪一层；插桩保持只读。
2. 用可论证的反应积分误差分析或收敛参考判断速率差异，同时检查能量增量、守恒和候选／实际步长。现有场接近不等于该速率已获准豁免。
3. 若确认核心接受或累计路径缺陷，提交独立、最小的共享实现补丁并回归；若属于受控数值差异，给出该字段专门的预算和依据，保留本次原始失败，不能调宽统一阈值或删掉字段后宣称通过。

GNN 现有输入白名单虽不含 `enuc_rate`，教师轨迹仍受物理与步长控制影响，不能因此跳过审查。训练放行继续为 false；独立矩阵仍为 5／60 条、4／18 个初态组，本轮问题复现不算新增独立样本。此前 CPU 先导的非干扰和条件收支结论保留，但不据此越过 CUDA 的当前审查项。

问题澄清后，再恢复 CUDA 观察器构建、同后端开启／关闭验证、五类先导与剩余矩阵。此处请求的是数值审阅和后续处置意见，不请求直接合并源码或发布训练结果。
