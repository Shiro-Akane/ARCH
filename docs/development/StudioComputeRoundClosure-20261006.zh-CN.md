# Studio / Compute 本轮冻结交付：真实 RZ 外源 owner / RK / checkpoint

日期：2026-10-06。分支：studio/compute-optim-integration。
交付父节点：1f743efd7cf7d0793a766f3c2cd4fdca1bc9cadd。
已 fetch 并完整阅读 compute/optim 的 b466ce0216928fee56878afe43aae9e8a1614f25 收尾清单与归并说明；未 merge/cherry-pick 该说明线、O8 或 main。
科学定义继续采用 CORE-RZ-20261006-v1 / 4639774fe3c94ae27b3e831d0f0b9d6340de34fd。

## 结论与边界

**私有 CPU 外源生产 owner 子集 COMPLETE；完整 RZ 科学 NOT_SIGNED。**

完成真实 Runtime 身份、实际 Hydro 重构 policy 的源 limiter 接线、全域准备与单次缓存消费、原 Euler/RK2/RK3 数值执行器接线、数值拒绝回滚，以及原冻结 24 组演化和 24 组真实 checkpoint 续算。
四项 finding 独立结算；外源子集不能替代 finite-ring、粘性、轴邻格或连续面力签收。

公开 Core 源码、配置/API/启动 gate、CPU/CUDA ARCH executable 均未改变。候选只在同一工作区忽略目录，交付自包含 inert patch，供 Core review 后按模块归入 compute/optim。
未开放能力，未开展 Windows、3D、重复计时或长跑。

## 精确候选身份

- 快照：326626cb1f51dc82ad78c5d59238b07e84d1cbbc；本轮父节点相关科学文件与其一致。
- 累积补丁：[rz-external-production-binding-20261006.patch](../../validation/gravity/candidates/rz-external-production-binding-20261006.patch)，23 文件；不要叠加旧 producer patch。
- patch SHA-256：dc1ec77c6ee5034f93ea45b309b1dbc311ac7be86d56b9baf36fb158d58ad97d。
- 私有源码：/home/arch/projects/ARCH-compute-optim/studio/.local/integration/rz-source-candidate-20261006/source。
- build：同级 build-cpu；target arch_curvilinear_metrics。
- ELF SHA-256：660634245f2a0e2ade20558e4bfbc8680da2b3bcabecb2d4149bb10e68bde0c5。
- ELF 链接真实 Runtime、Hydro/RK、AMR 和 checkpoint/IO owners，但它是内部 qualification fixture，**不是新公开 ARCH binary**，不代表完整应用端到端支持。
- GNU13.3、Release、CPU、KLU ON/CUDA OFF、OpenMP。flags/cache/逐文件摘要见 [identity.json](../../validation/gravity/results/rz-frozen-closure-20261006/identity.json)。
- 复用既有 HighFive 本地依赖；未重配置公开 build tree。
- 构建 guard：available RAM>=4096MiB、swap 增量<=256MiB、单次1800s、parallel28。最终编译约16.05s、owned RSS峰值约3.26GiB、swap增量0；这不证明2D运行guard。

git apply --check 已确认 patch 可从当前公开源码独立应用。候选 canonical source root 的 tools/audit_architecture.py 返回0，未改规则或排除 implementation 文件。此次只审计候选源码树，不把包含忽略构建目录的扫描冒充生产审计。

## 身份、重构与缓存

RzExternalStage 从实际 DriverRuntime 构造 backend/chart、Host allocation-family generation、配置 owner lease、macro step/time、handle/slot/ledger version、ghost 可读性，以及 scheduler canonical descriptor。
配置 lease 来自 Runtime 单调 issuer，非 fixture 常量；仅 Runtime-local，不宣称跨进程全局唯一。配置完整快照必须与实际 source policy 一致。identity 含实际阶段输入时间和dt，拒绝非规范RK权重/time。

Host generation 随实际拓扑提交/重网格更新；pointer不冒充generation。cache绑定独占owner，ExternalGravity禁止复制/移动活动状态。
正常路径与11个拒绝反例通过：过期macro/time、配置、source、version/ghost、storage、descriptor、重复owner/消费、后续block迟发拒绝等。

PCM使用实际NoLimiter；MUSCL接实际limiter；未有获准源积分路线的reconstruction明确拒绝，无静默fallback。冻结演化验证PCM，不宣称所有MUSCL/PPM或CUDA RZ合格。
所有block先准备，成功后一次发布batch；数值stage只消费同一input/frame/dt一次。接受源预算用真实RK register weight，不以终态反填功。
W/V积分测度沿Core v1，不添加heating、模型白名单或旋流特设平衡。

## RK 数值拒绝保护

复用原Euler/RK2/RK3、tableau、BC、flux/reflux和ledger publication。
私有CPU宏步事务保存Current/Next/Scratch（含repair）、ledger/high-water/epoch、clock、flux-register内容/拓扑及已接受源账本长度。

注入故障在**最后RK stage第二block完成真实Hydro evaluate_patch后**，此前block与此前RK阶段已执行数值写入。
三种方法拒绝均恢复数组、ledger、clock、flux和源账本；有效重试完成10步。不是只证明preflight无写。
snapshot有内存开销，仅私有qualification；不声称性能优化。实际Runtime内部regrid probe的阈值设置独立于冻结包，不改冻结输入。

三个实际编译错误变体均被执行测试拒绝：删除数值回滚；PCM源误用MinMod；允许cache重复消费。
编译失败不算拒绝；源码字节与ELF SHA完全恢复后才执行最终冻结包。
见 [mutation-summary.json](../../validation/gravity/results/rz-frozen-closure-20261006/mutation-summary.json)。

## 原冻结演化及真实 checkpoint

保持r=[1,3]、z=[-1,1]、5个mixed-AMR leaves、2 species、g_phi=±0.025、dt=1e-4、10步。
24组合=径向/轴向coarse-fine方向0/1 × closed/open × 两符号 × Euler/RK2/RK3。
原normalized预算1e-12不变，未改终点/网格凑PASS。

| 指标 | 24组最坏normalized error | 原阈值 |
| --- | ---: | ---: |
| J含外源和边界预算 | 6.0936030468846e-16 | 1e-12 |
| mass | 6.192004350566235e-16 | 1e-12 |
| energy含源功和边界 | 7.337112182987442e-16 | 1e-12 |
| species | 6.687096555117187e-16 | 1e-12 |

repair=0；真实stage预算与独立fixture源/边界预算一致，closed/open沿冻结定义。

各组step5/t=.0005通过真实DriverIO/ChkIO/HDF5Writer写checkpoint，读回FP64状态，重新创建恢复AMR control/controller/BC/Runtime/source owner，续至step10/t=.001。
每组最终10,240 native words与连续演化逐bit一致，checkpoint SHA前后不变；24/24 PASS。
限定CPU外源文件续算不泛化到finite-ring、viscous、CUDA或重网格演化轨迹。

探索中fixture的io.t_max默认0曾导致错误t=0 checkpoint，也曾因未补全IO/repair owner发生拒绝；这些原始产物留本地，不计PASS。最终runner显式断言真实split/final time、step和native word extent。
24演化/24checkpoint身份见 [summary.json](../../validation/gravity/results/rz-frozen-closure-20261006/summary.json)。
raw H5/checkpoint/ELF和全量logs留studio/.local/integration，不提交。

## 四项 finding

| 项目 | 状态 | 实际结果 / 未完成原因 |
| --- | --- | --- |
| finite-ring | BLOCKED | 既有actual Runtime root三槽、内部regrid/rollback条件候选通过；接触/源内连续参考没有合格误差界。density/topology身份不等于物理力签收，本轮external不替代finite-ring |
| RZ-VISC-01 | NOT_RUN | legacy curvilinear regression通过，但仍历史vector-Laplacian。Core v1定义已批准；共享DiffFlux对称tau_rphi/tau_zphi与配对torque/energy迁移未在本节点实现/验证，非等待重复批准 |
| RZ-AXIS-01 | FAILED | 最终ELF重跑原Ω=1、N16/32/64/128 axis/off-axis RHS；轴上最细p_Linf=.999997<原>=1.8，离轴1.99441。一般V/W重构/kinetic/EOS/source求积未解决；Ω4/cubic/local-band/mixed-AMR演化续算未完成，无调norm或专用平衡补丁 |
| RZ-CONTINUOUS-FORCE-REFERENCE-01 | BLOCKED | 既有匹配源88cells/208faces参考，Gauss16→32漂移约2.55e-9cm/s²；漂移不是certified bound，未建立<=原阈值/10误差界。Core v1参考对象已确认，缺实现/认证证据 |

逐项已有证据与原因：[findings.json](../../validation/gravity/results/rz-frozen-closure-20261006/findings.json)。
axis工具预期exit2是保留失败，不能因收尾runner识别失败正确而标科学PASS。

## JENS、2D与公开状态

既有私有CUDA JENS uniform-lifecycle-1 scoped/冻结演化/真实续算证据保留、不重跑；公开gate不变，不扩展其他生命周期。

2D诊断NOT_RUN：实际physical-core/type映射及RAM/owned-GPU/新产物/next-write headroom完整guard未建立。
build memory guard不能替代运行guard。未启动无保护试跑，保持1e-7s终点/split5e-8s、单条1200s、组2400s、RAM12GiB、GPU10GiB、disk10GiB原条件。
没有3D、重复计时或长包。

| 公开项目 | 变化 |
| --- | --- |
| Core源码/配置/API/capabilities | 无；候选patch未应用 |
| RZ及CUDA JENS公开gate | 保持 |
| CPU ARCH SHA | 32f7b13972ac9381076b06e04c581900065a246796e911bdd161479d86f31b8e，不变 |
| CUDA ARCH SHA | 1cbbd6952f4970f89ef5071b9e3f18d9ec619ec92824f0ad9928455f6e27eb53，不变 |

## 复现与停止

在同一工作区忽略目录，以326626cb...的git archive重建完整候选源码，再仅向私有目录应用本次自包含patch，不叠旧producer patch。
S/B必须位于本工作区studio/.local/integration。实际配置与命令如下：

~~~bash
cmake -S "$S" -B "$B" -G Ninja   -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_CUDA=OFF   -DARCH_ENABLE_KLU=ON -DARCH_FETCH_SUITESPARSE=OFF   -DBUILD_TESTING=ON   -DFETCHCONTENT_SOURCE_DIR_HIGHFIVE=/home/arch/projects/ARCH-mainline/build-phase3a-cpu/_deps/highfive-src   -DARCH_RUNTIME_OUTPUT_DIRECTORY="$B/bin"
python3 tools/run_memory_guarded.py --min-available-mib 4096   --max-swap-growth-mib 256 --log "$B/guard.log" --   timeout --signal=TERM --kill-after=10 1800   cmake --build "$B" --target arch_curvilinear_metrics --parallel 28
python3 validation/core_contracts/rz/run_production_binding_mutations.py   --source "$S" --build "$B" --output "$NEW_LOCAL_MUTATION_ROOT"
python3 validation/core_contracts/rz/run_frozen_rz_closure.py   --source "$S" --build "$B" --output "$NEW_LOCAL_CLOSURE_ROOT"
python3 tools/audit_architecture.py "$S"
~~~

新结果目录必须不存在；runner拒绝复用或source/build错配。
本机最终结果：studio/.local/integration/rz-production-mutations-freeze-20261006及rz-frozen-closure-final-20261006。

本轮按清单交付**部分完成的review checkpoint**，完整RZ未签收。
仅提交inert patch、复现脚本、处理后指标/身份和报告；根STATUS不变，已有Studio/Host/Plotfile成果保留。
冻结后由Core按模块处理O8/compute冲突，不整分支merge，不退役Phase分支，不创建新release tag。
