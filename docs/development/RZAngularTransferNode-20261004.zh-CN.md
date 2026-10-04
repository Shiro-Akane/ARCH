# RZ-A CPU 全单元传递节点

依据 Core 86bec324 第8节，接续 e4e1fca9 共享几何节点。
本节点交付真实 full-cell restriction/prolongation 和 tree candidate parent，
不是完整RZ科学签收；coarse/fine ghost、hydro/viscous/reflux、Init/IO/checkpoint
等消费者尚待顺序贯通，公共 RZ 能力门槛继续保留。

## 数学与职责

共享 RegridTransferMath 的显式 angular_momentum geometry：
m_phi 使用 coarse/fine W，rho、径/轴动量、E、ENUC、rhoX继续使用V。
restriction 直接 sum(m_phi W)/W，不计算无用 m_phi V 中间量。
prolongation 在 W centroid 的物理径向位置重构m_phi，以W纠正其零积分偏差；
其他守恒量使用V纠正。RZ fluid和rhoX使用同一family-wide theta，
不独立clip J/E/组分，也不靠加热、floor或改阈值解决不可解析父态。
无第二权威J/ell数组，没有复制科学kernel。

GridMetrics负责所有W、centroid和ghost镜像位置；
ghost位置仅为重构坐标，不构造负V/W。
CPU Block adapter 提供geometry；共享scalar math仍为Host/device可编译，
但CUDA adapter尚未绑定W，不宣称CUDA RZ或JENS已验收。

## 同一候选父态

Tree保存显式root chart，细化/合并的geometry、MinimumJeansCells和实际migration使用它。
CandidateParentResolved调用真实Block TryAverageToCoarse，
CoarseFluid仅veto此组，不分配/发布pool parent；其他输入或数学故障继续报错。
同一W/V限制定义得到的候选父态先admissibility，再真实EOS/JENS，
实际migration同样调用该共享restriction；没有额外V平均JENS父态。
未开放或未完成的公共chart不因该内部CPU路径而自动成为支持能力。

## CPU证据

- 正子态/负父态反例：W1:W2=1:7，V1:V2=1:3，
  m_parent=-111/8，E_parent=1539/16，internal=-9/128。
  真实tree ordinary coarsen与JENS coarsen都veto，原fine E/J/rhoX逐数组不变，
  active IDs和pool数不变，非法父态不进入EOS。
- 合法父态：真实IdealGas/JENS接收轴首单元m_phi=15/8，
  明确区别于V平均7/4；最后发布的parent与该候选一致。
- 刚性转动：实际Block细化复现每个子格W centroid值，
  J与独立full-ring analytic integral相对差<1e-12。
- varying swirl + rho/species：轴域J变化 -1.1829502495566292e-17，
  非轴域 -2.2651312806152061e-17；原V守恒量与J分别断言<1e-12，
  不再把mom_phi V积分称为守恒量。
- 人为强旋流gradient触发共同theta；每个子态可解析且J、E、rho、rhoX积分保持，
  实际rhoX偏差与m_phi使用同一theta。
- RZ JENS独立静态父态重新加入非零旋流并独立积分W。
  6例最大相对误差2.4583812499529164e-16，原传播界4.8849813083506904e-15不变。
  旧V参考不能作为本节点PASS；首次参考仍残留旧kinetic表达式的失败已查清并修正。

## 构建、回归、身份

受内存guard的既有CPU Release增量构建PASS，无configure/复制build tree。
最终binary SHA256：b5f6a744dc4a57add3df3ff8d6051a818f5112f7445e12ae22f231c4eb56dd80。
source HEAD at build=e4e1fca9 + 本节点补丁，repositoryDirtyAtBuild=true；
源码fingerprints和实际run目录见处理后summary.json。
没有更换Studio managed binary或伪造其Build Manifest。

amr_operation_plans、jeans_diagnostics、curvilinear_metrics、topology_transaction、
plotfile_publication、checkpoint_compatibility、self_gravity_lifecycle、
configuration_api_contract、真实source architecture和diff check PASS。
实际ARCH改变后，以最终ELF重跑uniform-lifecycle-1：
9组CPU Cartesian演化及9组checkpoint续算精确对照PASS，
关闭/output-only原生状态和solve counts精确一致，mass/E drift=0。
这只证实既有冻结JENS短包回归，不宣称其他EOS、非均匀、完整RZ或CUDA科学验收。
所有H5/plt/checkpoint/ELF和全量日志留本机；提交processed metrics与源码，不上传原始场。

## 下一节点

继续A未迁移的AMR ghost消费者，然后B的torque/source/reflux/axis/viscous，
C的Init/BC/EOS/diagnostics/plot/API及checkpoint意义升级、旧RZ拒绝，
D完整CPU科学后统一CUDA，最后按冻结输入/终点启动对应长跑和计时。
有限环体按第7节共享职责推进，近场estimate不冒充certified bound。
原1e-12 angular budget及其他科学门槛不改变，Windows适配不开展。
