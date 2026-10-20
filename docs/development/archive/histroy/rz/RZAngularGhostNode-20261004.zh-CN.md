# RZ-A CPU coarse/fine ghost 节点

依据 Core 86bec324 第8节，接续 f5810df7 的全单元传递。
本节点只完成共享 Host ghost 消费者的 W 接线，不表示完整 RZ 科学签收。

## 根因与共享职责

旧 coarse/fine ghost 对全部动量使用 V average，并在逻辑 ±1/4 位置重构，
未消费 m_phi=J/W 的物理定义。现在显式 RZ path 复用 RegridTransferMath：
m_phi 使用 W restriction、W centroid 重构和 W 零积分偏差；
rho、径/轴动量、E、ENUC、rhoX 保持既有 V 语义。
prolongation 使用与全单元传递相同的 family-wide theta；
没有新增权威 J/ell 数组，也没有第二套科学公式。

GhostExchange 只负责从实际 donor/destination 网格借用几何、选择虚拟子单元和发布结果；
W、重构位置与轴镜像全部由 GridMetrics 提供。
缓存仍属于 topology，实际状态 bounds 每次借用，不因缓存而陈旧。
Driver boundary/runtime/regrid、Euler/RK2/RK3、AMR diffusion synchronization
传入当前配置 bounds；regrid payload ghost 同时补上真实 chart identity。
非 RZ path 保留原逻辑。

## CPU fixture 与失败传播

径向/轴向接口、含轴/非轴域、Current/Next/Scratch 三槽：
1536 次 injection 与768次 average 均匹配独立刚性转动 W-centroid 参考，
容差1e-12，active科学状态不改变；V字段和组分同时检查。
配置 internal ceiling 越界、Core 冻结的正子态/负父态反例均明确拒绝，
完整 coarse/fine plan 在 scatter 前失败，所有相关数组逐值不变。
不加热、不floor、不更改 J/E 或验收阈值。

这里保证的是 ExecuteCoarseFinePlan 的 gather-before-scatter；
完整 ExecuteExchange 先执行 same-level scatter，不能据此宣称整个 exchange 全局原子。
尚未迁移的 hydro/viscous/reflux 和 checkpoint 等消费者不能沿用本节点 PASS。

## 构建与回归身份

既有 CPU Release build tree 增量构建，parallel=28，memory guard PASS；
无重新 configure、build tree复制或新工作区。
实际 ARCH SHA256：a6601b115a75d1f79de61aed593ac4b0f14adea8994849e67dfd3cb4924ebeb6。
build 时 source HEAD=f5810df7d763b6780f7c5bded03788f29060fb59 + 本补丁；
repository dirty=true，精确 delivery fingerprints 见 summary.json。
未更换 Studio managed binary，也未伪造其 Build Manifest。

amr_operation_plans、curvilinear_metrics，以及实际 ARCH 的
self_gravity_lifecycle、runtime_probe_and_capabilities、preview_initial_conversion、
topology_transaction、preview_api_contract、preview_session_contract 全部 PASS。
source architecture 与 git diff --check PASS。

由于 binary 身份改变，以新 ELF 复验 uniform-lifecycle-1：
1D/2D/3D × disabled/output-only/active 九组演化和九组真实0.01→0.02s续算 PASS；
checkpoint 原生/controller payload 精确一致，mass/E drift=0，
disabled/output-only 原生字段与 solve counts 精确一致。
该短包不覆盖非均匀态、其他 EOS、完整 RZ 或 CUDA。

## 数据与后续门槛

处理后指标：validation/amr/results/rz-angular-ghost-20261004/summary.json。
全部 H5、plt、checkpoint、ELF、全量日志留本机
studio/.local/integration/rz-angular-ghost-20261004/。
提交内容只有代码、fixture、摘要和说明。

继续 B 的 hydro torque/source/reflux/axis/viscous，再 C 的 Init/BC/EOS/IO/API/
checkpoint 意义升级和旧 RZ 拒绝，最后 D CPU 科学→统一 CUDA→已冻结长跑。
有限环体按第7节共享 owner 和 error ledger 推进，estimate 不冒充 certified bound。
完整 RZ 和 CUDA 门槛保留；不开展 Windows 适配。
