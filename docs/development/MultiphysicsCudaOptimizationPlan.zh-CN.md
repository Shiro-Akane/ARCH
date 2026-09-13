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
| V1：燃烧正确性门槛 | 定位 150 的 BD/ENUC 首个分歧；检查接受/拒绝路径、增量能量和原矩阵残差；排除 CPU 参考自身误差 | 不放宽 2e-10 原场预算；150/200 三 ODE、池尾部/存储更换通过后再扩大容量/轨迹；补完整应用与独立参考 | 有界残差修正通过 150/200 原四步、2/3 单元存储更换及附加失败合同；容量/长期/全应用尚未验收 |
| P1：燃烧执行层 | 先分解 RHS/Jacobian、矩阵装配、symbolic/numeric factor、solve、同步与内存成本；小网络批量和大网络有界 lane 调度分别优化 | 逐个可回退改动；不复制 ODE；缓存显式绑定 matrix token/世代；不接受残差失效的解；报告冷启动和稳态及全应用规模扫描 | 批量完成/回传已实现并通过逐位设备对照；有界 factor cache 通过 150/200 的 32/33 单元、8/32 lane BD/ROS4 四步；全应用计时/长期/最终 cuDSS 集成待验收 |
| P2：扩散执行层 | RKL1/RKL2 stage 的跨块提交、系数/通量临时存储复用、dt/status 批量归约 | 所有方向、曲线/Cartesian、负 gamma、F(Y0) 跨步隔离、AMR reflux 和 restart 原回归；与同轮 CPU 对照 | 批量 dt/copy/stage 已实现；真实设备逐位一致、canonical 解析解/原收敛要求通过；完整 AMR/长期/restart 与计时继续中 |
| P3：四模块耦合 | Hydro＋burn＋diffusion＋动态 AMR，真实 EOS、网络、算子顺序和燃烧 limiter | 质量/电荷及含源项能量、组分演化、接受步/子步工作量、regrid、split-run、CPU↔GPU restart 和 restart 后 regrid；同输入全应用计时 | 三 ODE × RKL1/RKL2 的六组真实 Helm/AMR 耦合数值门槛通过；联合 restart/长期和性能尚待验收 |
| S5：按剩余热点深调 | 有证据再做 Graph 或 kernel 调优；可分别用于合格模块，不要求强行采用 | 含建图/失效/更新/额外显存的端到端收益；保留普通 CUDA；无收益记录拒绝/延期 | 条件性；不是绕过 V1 的入口 |
| V2：联合收尾 | 合格版本 clean 构建、完整回归、性能与内存总结、双端交付 | memcheck/racecheck、失败注入及长期资源增长；适用资源上的 Debug 构建门槛；证据缺失明确标受阻，不发完整验收标签 | vGPU 调试限制仍阻塞安全资格 |

V1 的失败不阻止只读扩散分析/准备；不把燃烧失败版本用于声称耦合性能已达标。不并发运行正式计时、编译和 cuDSS 压测，不终止他人作业。所有阶段有明确 timeout、Host RAM/显存观测和失败日志。

## 实验协议

1. 原基线和候选使用各自不可变源码/构建/二进制身份；同一版本内 CPU/GPU 使用相同输入、EOS/网络、物理时长、输出与比较口径。测试脚本重编和生产重编分别记录。
2. 单独做 profiling，不把插桩结果混入正式速度中位数。先预热，再至少 5 次交替正式测量；报告原始样本、中位数和离散性。CPU 至少包含合理线程扫描和亲和性设置，不能专挑较慢 CPU 点。
3. 冷启动（CUDA/context、网络/EOS 上传、分析/分配）与稳态分列，再报全应用端到端。同步/API 耗时可能包含设备等待，不直接相加推算加速上限。
4. 150/200 先原四步，再有界容量与长步、完整应用；不从 2/3 单元微测试外推全网格速度。记录实际 workspace 容量、分解次数/复用、显存高水位，不能无限扩大 factor 池换速度。
5. 验证失败先定位。任何参考/比较器/预算变化都独立审查，不与候选优化混在同一通过声明里。允许 test-only shadow solve/高精度残差诊断，但不冒充生产路线通过或独立反应物理 oracle。

## 2026-09-14 实施入口核对

- 燃烧正确性：已隔离出原系统残差拒绝造成 BD 多子步的触发链；设备驻留、有界两轮残差修正通过 audit150/200 原四步三 ODE、provider 合同和两个原失败路径回归；原预算不变。不提前标记 V1 的容量、长期和全应用验收完成。
- 燃烧 kernel 资源：独立 runtime attributes 探针观察到 audit150 BD 的 `advance_ode` 每线程 47152 bytes local memory、255 registers；2/3 单元诊断只发 1 block × 32 threads。这是局部状态/利用率分析入口，不是耗时归因，也不能外推全网格性能。
- 燃烧调度：`SparseOdeBatch.cuh` 当前只有一个 resident factor owner，不同 lane/token 可相互驱逐因子。优先量化恢复分解次数和 factor cache 的真实显存成本，再比较有界复用方案；不能用 2/3 单元微测试掩盖生产池最多一个 warp 的行为。
- 扩散 dt：`Driver.h` 逐块调用 `compute_diffusion_dt`；`CudaBackendMicrophysicsControl.cpp` 每块下载 int/double 后 `quiesce()`。可沿已有 Hydro batch 完成契约合并回传，但必须保留每块错误及确定性全局 minimum。
- 扩散 stage/copy：同一控制文件的 `copy_state_slot`、`execute_diffusion_stage` 仍逐块完成等待。批量化需要保留每 stage 的 ghost → operator → register → reflux → publish 顺序，不能跨越 RKL 依赖合并。
- 编译成本：旧 focused 记录中，两个 `test_generated_sparse_burn_factory.cu` 实例约耗时 3575/6756 秒。当前 provider 私有接口内的修正可重编两个小 TU、重链接旧对象验证；这不替代最终 clean/full build，也不允许假改旧产物的源码身份。

## 协作和同步

沿用固定分支 `codex/hpc-cuda-optimization`，本地 commit 后向 `personal`（Arsenic-er）和 `friend`（Shiro-Akane）显式 fast-forward push 同一个 SHA；不碰 main，不强推。开始/发布前 fetch，遇别人新提交先整合和复测。按总计划第 9 节，诊断/开发快照可同步供协作，但“同步”不等于“验收”；只有相应证据完整才能标记已验收，不给失败阶段发验收标签。

## 当前证据入口

- [S1–S4 数值验证](../../validation/backend/results/hpc-cuda-optimization/S4/validation-20260913/README.md)：24/24 合同/设备测试、121 组 checkpoint。
- [完整线程扫描](../../validation/backend/results/hpc-cuda-optimization/S4/timing-20260913/README.md)：120 次运行、140 组线程内＋16 组跨线程比较；最快 CPU16 同线程比 1.588/1.667，仅无燃烧 Sedov。
- [150/200 原燃烧诊断](../../validation/backend/results/hpc-cuda-optimization/burn-status-20260913/README.md)：200 focused gate 通过；保留原 150 BD 首个 ENUC 相对差 1.378861e-9 的失败基线。
- [V1 原系统残差修正](../../validation/backend/results/hpc-cuda-optimization/V1-residual-20260914/README.md)：150/200 focused 三 ODE 全通过，150 BD 最大场差降至 1.796e-14；无库/ODE/预算改动。附加失败合同通过；资源探针单独记录，不混入正式性能。
- [P1/P2 执行层当前证据](../../validation/backend/results/hpc-cuda-optimization/P12-microphysics-20260914/README.md)：串行完整 CUDA 构建、24 个合同、canonical 7 算例、燃烧独立时间积分与大网络容量门槛；包含被否决的 64 MiB 缓存淘汰设计和原始日志。开发记录不等于联合验收。
