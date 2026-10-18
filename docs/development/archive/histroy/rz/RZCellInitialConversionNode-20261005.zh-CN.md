# RZ C：原生单元 V/W 初态转换候选节点

基线 ceffbaafe6f849a15dc7036a1f8a013ee668eb5a。
依据科学清单8.1/8.4继续共享平均语义迁移。本节点完成候选共享数学和
独立初始化闭合验证，**没有把实际 PopulateState 切到新转换，C尚未完成**。

## Finding 与实现

RZ-INIT-W-01：实际 ProblemHelper::PopulateState 仍在 midpoint 调 Init，
以 rho*u_phi 直接赋 mom_w，不能代表 J/W。例如 r=[1/2,1]、rho=1、
u_phi=Omega*r，旧值0.75Omega，批准解析值45/56Omega。该 finding 未关闭。

GridMetrics::Rz::CellAverageSamples 是 allocation-free Host/device tensor Gauss-2
几何叶函数：V 使用 r 权，W 使用 r² 权，分别归一化；不增加用户求积控制，
不是任意非光滑 Init 的精确积分或误差证书。

现有 InitialStateConversion owner 新增 InitialRzCellState：
4次物理 primitive callback → 原共享 InitialConservedState/EOS →
rho、mom_r、mom_z、E、rhoX 的 V 平均 / mom_phi 的 W 平均 →
同一代表守恒态严格 admissibility 和 EOS T/P/c_s 检查。
rhoX 最后除平均rho得到 X；不另存可演化 J/ell。
callback species extent变化、非法cell在返回候选前拒绝。
需要 point repair 或单元平均后内能不可解析时明确失败，不补热、不夹角向态，
不改 E/J，也不增加 floors。结果只在 scratch 返回，不改活状态。

RZ-INIT-REPAIR-01：旧 PopulateState repair 账本目前统一用 V 乘所有 momentum
delta；安装新的 W 平均转换时须同时处理角向 J/W 表示与真实 ghost发布。
不能把新候选的“拒绝 repair”行为悄悄代入旧通用初始 repair 契约。
本次没有修改现有修复数学或旧 fixture，保留这项真实集成缺口。

## 独立验证

现有 preview_initial_conversion fixture 扩展6例：
轴邻[0,0.125]、非轴[0.5,1]、annulus[4,4.25]，pressure/temperature输入；
rho=2，u=0.5r，v=0.25z，u_phi=2r，线性z双组分。
参考从独立 long-double 单项式原函数计算，不调用生产权矩/权重。
最大算术误差1.42109e-14，沿用既有2e-12工程门槛。

Core给定刚体参考 m_phi/Omega=45/56、E_rot/Omega²=5/16通过。
高旋流/低热压的所有 sampled point 可解析，但平均后的代表闭合不可解析；
候选明确拒绝。显式density floor修复样本也拒绝。
负半径cell、underflow后V/W归零、有限bounds但轴向span overflow均在callback前拒绝，
不用负ghost测度或epsilon冒充正V/W。后两项是审阅新增反例，独立保留首轮日志；
最终preflight增量build guard14.147s、peakRSS1925724KiB、无swap，转换CTest复验PASS。
curvilinear权矩/旧PopulateState未被该preflight修改，沿用对应已通过证据。

原生V RMS的初始化代表内能闭合误差（包括轴邻格）：
16/32/64 cells为约1.62608e-4、4.06805e-5、1.01719e-5，
order约1.99898、1.99975，满足Core既有至少1.8要求。
这只是初始化离散闭合一致性，**不是 evolved hydro、径向平衡或力的收敛验收**。
stdout默认6位有效数字，摘要未冒充更高精度。

CPU preview_initial_conversion / curvilinear_metrics 2/2 PASS；
真实 PopulateState/EOS scoped runner 原polar/RZ × normal/repair共4例PASS。
后者证明旧行为未被改变，不证明新conversion已接入。
架构审计/diff check PASS。未新增CI或完整长期测试矩阵。

## 身份与边界

现有 build-cpu Release 增量编译 ARCH和两个相关目标通过，parallel28，
peak RSS9259648KiB，guard25.525s，无swap，未configure。
实际production ELF仍
b0f38076e245eb78c335b2324930f16d46cd952304aec5909ac100fe922812ad；
新候选尚未被生产调用，既有路径重编未改ELF。
沿用此ELF匹配的上一节点JENS冻结9+9 receipt，不无变化重复跑。

处理后证据 validation/amr/results/rz-cell-initial-conversion-20261005/summary.json。
全量日志/对象/ELF留本机 ignored studio/.local/integration/rz-cell-initial-conversion-20261005。
下一步仍须将 Init physical interiors、ghost 与 repair身份按同一契约真正贯通，
再验证注册模型和受影响科学子组；不以本候选替代该最终要求。
RZ-VISC-01、RZ-AXIS-01、完整C/D和环体组合账本继续待审；
production RZ gate保持，没有CUDA、长跑、Windows、新tag/main merge/newworktree。
