# 多物理 CUDA 优化执行与验收计划

日期：2026-09-14。依据：用户提供的合作者聊天意见，以及既有 [HPC＋CUDA 总计划](HpcCudaOptimizationPlan.zh-CN.md)。这是执行计划，不是承诺所有规模必定加速或已完成验收。

## 范围与目标

本轮覆盖已有流体、燃烧、扩散、AMR 及其耦合路径。EOS 是这些路径的依赖，性能检查不能跳过真实表 EOS；不新增引力/广相物理、MPI、多 GPU 或更换依赖库。现有相关模块的回归仍需保留。

性能目标为代表性工作量上 GPU 相对合理 CPU 基线稳定加速，至少争取持平。记录小/中/大规模交叉点；150/200 核素若不能持平，必须交付实际差距、瓶颈和适用边界，而不是减少核素、隐藏小算例、静默回退 CPU 或放宽容差。151/201 阶是逐燃烧单元的系统，不是全网格全局矩阵；未来引力求解不能未经测量照搬该结论。

继续共用 Host/Device 物理数学。保持 EOS、网络、ODE、AMR、输出频率、严格浮点和 restart schema。KLU 外部实现不改；不默认换求解器、开 fast-math/混合精度或提高 CUDA 最低版本。科学预算和独立参考沿用各模块已有标准，不把 Sedov 的守恒条件直接套给有源项的燃烧。

## 分阶段执行表

| 阶段 | 工作 | 验收与交付 | 当前状态 |
|---|---|---|---|
| V0：冻结当前证据 | 归档 S1–S4 数值/线程扫描；收齐 150/200 原四步、源码/构建身份和失败日志 | 区分正确性、性能、安全、集成和同步；保留全部样本/失败；两端同 SHA | 已在 7abe86d4 双端归档；保留原 150 BD 失败及诊断 |
| V1：燃烧正确性门槛 | 定位 150 的 BD/ENUC 首个分歧；检查接受/拒绝路径、增量能量和原矩阵残差；排除 CPU 参考自身误差 | 不放宽 2e-10 原场预算；150/200 三 ODE、池尾部/存储更换通过后再扩大容量/轨迹；补完整应用与独立参考 | 原四步三 ODE、BD/ROS4 有界容量长轨迹及完整 ARCH＋Helm 原六算例／48 次 CPU/CUDA 运行通过。BE 的 32/33 非均匀单元、16 步至 1e-9 原 1800 秒超时仍保留；不以短时完整应用通过代替它，也不声称独立弱反应能量 oracle 完成 |
| P1：燃烧执行层 | 先分解 RHS/Jacobian、矩阵装配、symbolic/numeric factor、solve、同步与内存成本；小网络批量和大网络有界 lane 调度分别优化 | 逐个可回退改动；不复制 ODE；缓存显式绑定 matrix token/世代；不接受残差失效的解；报告冷启动和稳态及全应用规模扫描 | 跨块 dense kernel、原科学门槛和 3/1024/1025 块逐位对照通过；三 ODE 正式 324 次运行/315 比较通过，128 块对最快 CPU 约 1.80–2.37x；有界 cache 的 BD/ROS4 32/33 单元、池 8/32、16 步至 1e-9 全过。完整 cuDSS 应用原六算例／48 次运行及正式 144 次运行／138 对照通过；但原 32 单元端到端 CUDA 耗时为 CPU8 的约 5.0–10.3 倍，性能未齐平，不称稳态性能通过。BE 长轨迹超时仍失败；固定页状态试验主要转移等待位置，未显示明确收益，未采用 |
| P2：扩散执行层 | RKL1/RKL2 stage 的跨块提交、系数/通量临时存储复用、dt/status 批量归约 | 所有方向、曲线/Cartesian、负 gamma、F(Y0) 跨步隔离、AMR reflux 和 restart 原回归；与同轮 CPU 对照 | 跨块 kernel/slot-copy 已通过 canonical、完整 AMR/曲线/长期/restart、33 组分全局 scratch 和混合 EOS 失败隔离。S5 加同一共享修复后的新正式 216 次运行/210 比较通过：128 块 RKL1 与最快 CPU 基本持平，RKL2 约快 1.35 倍；小规模仍是 CPU 更快。新旧版本结果分开保存 |
| P3：四模块耦合 | Hydro＋burn＋diffusion＋动态 AMR，真实 EOS、网络、算子顺序和燃烧 limiter | 质量/电荷及含源项能量、组分演化、接受步/子步工作量、regrid、split-run、CPU↔GPU restart 和 restart 后 regrid；同输入全应用计时 | 原两组六方法及跨后端 split-run 回归通过。扩大规模发现的两项共享组分问题已修复；原 15 类回归及六方法 × 8/32/128 块的 18/18 组全输运耦合对照全过，原预算未变；正式耦合性能尚未验收 |
| S5：按剩余热点深调 | 有证据再做 Graph 或 kernel 调优；可分别用于合格模块，不要求强行采用 | 含建图/失效/更新/额外显存的端到端收益；保留普通 CUDA；无收益记录拒绝/延期 | S5a 每波 3/4 kernel、有界 scratch，保留 NaN/EOS 检查。加共享组分修复的生产候选已通过原 15 类和 18 组扩展回归；独立 clean 内置 Debug 在 16 GiB、零 swap 下完整构建通过。同修复产品双端备份完成；11 模块正式计时中两种扩散已完成并归档，三燃烧及六耦合继续执行，不称全部完成 |
| V2：联合收尾 | 合格版本 clean 构建、完整回归、性能与内存总结、双端交付 | memcheck/racecheck、失败注入及长期资源增长；适用资源上的 Debug 构建门槛；证据缺失明确标受阻，不发完整验收标签 | vGPU 调试限制仍阻塞安全资格 |

V1 的失败不阻止只读扩散分析/准备；不把燃烧失败版本用于声称耦合性能已达标。不并发运行正式计时、编译和 cuDSS 压测，不终止他人作业。所有阶段有明确 timeout、Host RAM/显存观测和失败日志。

## 实验协议

1. 原基线和候选使用各自不可变源码/构建/二进制身份；同一版本内 CPU/GPU 使用相同输入、EOS/网络、物理时长、输出与比较口径。测试脚本重编和生产重编分别记录。
2. 单独做 profiling，不把插桩结果混入正式速度中位数。先预热，再至少 5 次交替正式测量；报告原始样本、中位数和离散性。CPU 至少包含合理线程扫描和亲和性设置，不能专挑较慢 CPU 点。
3. 冷启动（CUDA/context、网络/EOS 上传、分析/分配）与稳态分列，再报全应用端到端。同步/API 耗时可能包含设备等待，不直接相加推算加速上限。
4. 150/200 先原四步，再有界容量与长步、完整应用；不从 2/3 单元微测试外推全网格速度。记录实际 workspace 容量、分解次数/复用、显存高水位，不能无限扩大 factor 池换速度。
5. 验证失败先定位。任何参考/比较器/预算变化都独立审查，不与候选优化混在同一通过声明里。允许 test-only shadow solve/高精度残差诊断，但不冒充生产路线通过或独立反应物理 oracle。

## 2026-09-14 实施入口核对

本节保留优化开始前的定位依据，不是当前代码状态。当前完成项以阶段表和证据入口为准。

- 燃烧正确性：已隔离出原系统残差拒绝造成 BD 多子步的触发链；设备驻留、有界两轮残差修正通过 audit150/200 原四步三 ODE、provider 合同和两个原失败路径回归；原预算不变。不提前标记 V1 的容量、长期和全应用验收完成。
- 燃烧 kernel 资源：独立 runtime attributes 探针观察到 audit150 BD 的 `advance_ode` 每线程 47152 bytes local memory、255 registers；2/3 单元诊断只发 1 block × 32 threads。这是局部状态/利用率分析入口，不是耗时归因，也不能外推全网格性能。
- 燃烧调度：`SparseOdeBatch.cuh` 当前只有一个 resident factor owner，不同 lane/token 可相互驱逐因子。优先量化恢复分解次数和 factor cache 的真实显存成本，再比较有界复用方案；不能用 2/3 单元微测试掩盖生产池最多一个 warp 的行为。
- 扩散 dt：`Driver.h` 逐块调用 `compute_diffusion_dt`；`CudaBackendMicrophysicsControl.cpp` 每块下载 int/double 后 `quiesce()`。可沿已有 Hydro batch 完成契约合并回传，但必须保留每块错误及确定性全局 minimum。
- 扩散 stage/copy：同一控制文件的 `copy_state_slot`、`execute_diffusion_stage` 仍逐块完成等待。批量化需要保留每 stage 的 ghost → operator → register → reflux → publish 顺序，不能跨越 RKL 依赖合并。
- 编译成本：旧 focused 记录中，两个 `test_generated_sparse_burn_factory.cu` 实例约耗时 3575/6756 秒。当前 provider 私有接口内的修正可重编两个小 TU、重链接旧对象验证；这不替代最终 clean/full build，也不允许假改旧产物的源码身份。

## 协作和同步

沿用固定分支 `codex/hpc-cuda-optimization`，本地 commit 后向 `personal`（Arsenic-er）和 `friend`（Shiro-Akane）显式 fast-forward push 同一个 SHA；不碰 main，不强推。开始/发布前 fetch，遇别人新提交先整合和复测。按总计划第 9 节，诊断/开发快照可同步供协作，但“同步”不等于“验收”；只有相应证据完整才能标记已验收，不给失败阶段发验收标签。

## 当前证据入口

### 扩大耦合规模后新增的门槛

原 15 项科学回归及小规模耦合通过不代表所有规模都通过。128 初始块的原
BD＋RKL2＋全输运＋AMR 输入暴露了 coarse→fine 将闭合舍入分配给痕量核素、
以及 MUSCL 独立限幅后面组分未归一化两处共享问题。修复沿用主导核素闭合和已有
PPM 面归一数学；不改 ODE、EOS、网络、终止时间或科学容差。
独立旧／新反例已验证；生产 CPU/CUDA 修复版均完整运行到原定 1e-10（706 步），
原组分、含源项守恒、字段及 regrid 工作量检查通过。修复后的原 15 类回归也已全过；
六方法 × 8/32/128 块的 18/18 组扩展矩阵已全过（36 次运行、18 次字段与工作量比较）。
这不代替后续正式计时或 150/200 核素完整应用验收。

正式计时使用同样数学修复的性能基线与 S5 候选，保留旧版 128 块失败且不计算其加速比。
六耦合方法的 8／32／128 块扫描、原 15 项回归和原预算均保留。

独立 [16 GiB 受限 Debug 构建](../../validation/backend/results/hpc-cuda-optimization/debug16-20260914/README.md)
已完成旧 S5 的内置 CUDA archive 与完整 ARCH 链接，禁用 scope swap、无 OOM。
旧证据不覆盖后续数值修复。另已完成本次修复版的独立 clean 内置 Debug 构建，
同样 16 GiB scope、禁用 swap、无 OOM；78 次编译调用成功，最大单编译进程 RSS
4,002,584 KiB。详见 [本次修复与 Debug 证据](../../validation/backend/results/hpc-cuda-optimization/composition-fix-20260914/README.md)。
两次资格均不覆盖 150/200 custom 网络；内核缺少总体峰值接口，未捏造峰值。

### 已归档结果

- [S1–S4 数值验证](../../validation/backend/results/hpc-cuda-optimization/S4/validation-20260913/README.md)：24/24 合同/设备测试、121 组 checkpoint。
- [完整线程扫描](../../validation/backend/results/hpc-cuda-optimization/S4/timing-20260913/README.md)：120 次运行、140 组线程内＋16 组跨线程比较；最快 CPU16 同线程比 1.588/1.667，仅无燃烧 Sedov。
- [150/200 原燃烧诊断](../../validation/backend/results/hpc-cuda-optimization/burn-status-20260913/README.md)：200 focused gate 通过；保留原 150 BD 首个 ENUC 相对差 1.378861e-9 的失败基线。
- [V1 原系统残差修正](../../validation/backend/results/hpc-cuda-optimization/V1-residual-20260914/README.md)：150/200 focused 三 ODE 全通过，150 BD 最大场差降至 1.796e-14；无库/ODE/预算改动。附加失败合同通过；资源探针单独记录，不混入正式性能。
- [P1/P2 执行层当前证据](../../validation/backend/results/hpc-cuda-optimization/P12-microphysics-20260914/README.md)：串行完整 CUDA 构建、24 个合同、canonical 7 算例、燃烧独立时间积分与大网络容量门槛；包含被否决的 64 MiB 缓存淘汰设计和原始日志。开发记录不等于联合验收。
- [P1/P2 跨块 kernel 联合版本](../../validation/backend/results/hpc-cuda-optimization/P12-kernel-batch-20260914/README.md)：完整 Release 链接、全套原科学/长期/耦合/restart 回归、波尾与 EOS 错误隔离；五模块正式 540 次运行/525 比较，含完整 CPU1/8/16 规模表与 raw 归档。大网络完整应用及 S5 另行验收。
- [S5a 指标批量数值检查点](../../validation/backend/results/hpc-cuda-optimization/S5-indicators-20260914/README.md)：独立构建和全部原数值门槛通过、完整原始数据归档；性能尚未验收。下一步是在此版本上做单模块与全输运耦合正式计时。
- [S5 同修复基线正式规模验证](../../validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914/README.md)：扩散两模块 216 次运行／210 次比较已通过；逐模块更新其余正式测试。原始 HDF／日志双端归档，大 TSV 无损压缩及 Git 投影差异显式记录，未通过关闭 trace 改变计时口径。
