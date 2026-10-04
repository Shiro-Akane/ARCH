# RZ 一般父节点区间与实际 native-face 消费

SCOPED CPU PASS；完整 RZ production gate 继续保持。本节点依据科学清单 §7.1/7.3，基线 d02f337da429333bbd23aab00dd2fd64597fdd15。

## 数学与实际所有者

在原 GravityBoundary 的十个 moments 布局和原 parent translation 公式上，新增瞬时区间 companion。每次由当前 source identity 绑定的真实 leaf density/edges 计算，反向 preorder 归约父节点；不增加独立演化或持久 moments cache。M、dipole、raw symmetric second moments 均保留含义。

叶为完整常密度环体。对显式 stored axis expansion center，使用真实 axial midpoint 的区间偏移（不假定 FP64 center 恰好等于实数边界中点），M、D_z、Ixx=Iyy、Izz 由解析环体积分区间给出。父节点复用 Q_ab(child)+d_a D_b+d_b D_a+d_a d_b M，translation 与 summation 都 enclosed。child support sphere 用 triangle inequality 平移到 parent，包含完整圆周；不是 r-z 半对角线。

一般父节点不假定中心反演对称；即使 dipole 为零也不能推断所有奇矩为零。所有 general nodes 使用已批准的：
    e_far <= G*M/R * q^3/(1-q), q=a/R<1
这里 source 非负，M 的可靠上界替代真质量，R 用可靠下界，support 用可靠上界。Legendre |P_n|<=1 的 n>=3 几何级数给出尾界。distance/support、moment、dipole/quadrupole evaluation、tail 与 reduction 的 FP64 区间都保留；非负源势的已知非正号仅收紧区间，不裁剪科学数组。

原单叶仍可使用经证明的 even q^4/(1-q²) 路径。没有把该 symmetry flag 暴露给父节点。signed manufactured source 不进入本节点的父接受路径；不把 abs(net mass) 当绝对质量积分。完整支持球内、非轴 center、无效/非有限 moments、无法表示的界均不获得 far certificate。

## 已接实际树查询，不是孤立公式

ring_boundary 现在沿现有 preorder/source tree 查询：只有完整 far interval 符合 source-count budget share 才跳过该 subtree，否则继续下树并调用原 finite leaf integral。最终 reduction/error 必须符合整个 face budget 才能 CertifiedAbsolute；预算失败仍不可发布。production values() 的 RZ throw 保留，不加直接加速度路径。

同一 source generation、density version、allocation、AMR topology、mesh/order checks 继续保护新路径。四个实际 uniform/mixed、axis/nonaxis engineering fixtures 使用原 face absolute target=1e-5 cm²/s²，不增加科学阈值：
- mixed=0, r_origin=0: leaves=16, faces=12, leaf_integrations=96, parent_attempts=60, accepted_parents=24, represented_leaves=192.
- mixed=0, r_origin=0.5: leaves=16, faces=16, leaf_integrations=184, parent_attempts=80, accepted_parents=18, represented_leaves=256.
- mixed=1, r_origin=0: leaves=28, faces=12, leaf_integrations=134, parent_attempts=86, accepted_parents=34, represented_leaves=336.
- mixed=1, r_origin=0.5: leaves=28, faces=16, leaf_integrations=286, parent_attempts=130, accepted_parents=30, represented_leaves=448.

represented leaves 严格等于每个外边界 face 的完整源数；无漏源/重复。far truncation upper 和 moment/geometry/distance/evaluation interval width 独立输出，最终 face error 包括 source reduction。上述 width 不是独立 EOS/force error，也不是可靠 quadrature-error 估计。RHS error 已通过原 canonical B/norm consumer，单位 s^-2；不能与 potential error 的 cm²/s² 直接比较。

预处理按 O(tree nodes) 扫描当前源，尚不承诺首次查询只与返回尺寸有关。原 maximum_leaf_evaluations=100000 cap 现在同时计 parent attempts 与 leaf integration，避免仅省 leaf count 却无界遍历父节点；保留原每叶 box cap。计数不代替 benchmark，不保证每个输入都更快。

## 独立参考与回归

独立 Python 以 stored binary64 edges/density 为精确输入，在 80/120 位直接做完整环体 polynomial integrals；不导入生产 moments/translation/tail。116 nodes、2320 moment comparisons 全在区间内，支持球包含真实源。4 个 source distributions 具有非零 axial dipole；24 个 general tail 以 120 位独立核对。

所有 24 个 far points 有 C++ full-azimuth Newton 8/12 诊断；8 个选定点另以 Python Decimal Newton 12/16、80/100位合计16次包含检查。阶差/precision差是诊断，不称 certified quadrature reference，不替代完整科学精度参考。

native-face、zero source、source-staleness、negative update、global WorkLimit、zero-target、old generation、mesh/order mismatch、失败输入不可进入 certified RHS，以及 production gate 全通过。最终 helper 额外拒绝非轴 center；最终 probe 完整 JSON 与原已验证数据 bit-identical，因此没有重复已完成的 Decimal campaign。

8 项受影响 CTest 通过：physical_constants、gravity_stage_contract、self_gravity_lifecycle、self_gravity_physics、composite_poisson_analytic/contract、poisson_multigrid_analytic/contract。ring-parent-probe/native-face/far-leaf/enclosure/axis-enclosure/boundary-ledger CLI 通过。架构与 diff check 通过。

## 构建身份和范围限制

复用唯一工作区、原 CPU Release tree，parallel=28 incremental build，memory guard 不停止、swap growth 0。最终 scoped probe SHA256：d78854ace1aa130c041dfdd255f72e2cdef116ad82fec364e76d6e2df39fb292。
production ARCH SHA256 仍为 b3f2c618d1912f1e2f0ae709dfab1e0af33b3f4302a9063d53dcbd680359cd06，与已发布 repair-ledger 节点的冻结 JENS 9+9 receipt 一致；不重复相同 binary 的冻结演化。

本节点“证书”明确限定 exact stored ring edges/density 与对应 point potential；尚不关闭 canonical physical geometry/volume/coefficient construction、完整 physical source + RHS/residual identity、实际 AMR source publication 或 Phi/force 科学精度。它不是完整 RZ 签收，不关闭 RZ viscosity/axis-force finding，不启动新 RZ CUDA/long-run 或 Windows 工作。

复现：
    build-cpu/arch_composite_poisson ring-parent-probe
    build-cpu/arch_composite_poisson ring-native-face
    python3 validation/gravity/rz_ring_parent_reference.py --probe build-cpu/arch_composite_poisson --output-root <new local directory>

原始 probe 数组、完整日志、ELF、H5/plt/checkpoint 留本机；只提交脚本、代码与标量摘要：
validation/gravity/results/rz-ring-parent-enclosure-20261005/summary.json。
