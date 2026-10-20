# RZ 非接触有限源积分可靠界节点（2026-10-05）

## 结论与范围

SCOPED CPU PASS，production RZ gate 保留。此前源矩形 Darboux 范围界仅随细分缓慢收缩；本节点在同一共享源积分 owner 内增加两点 tensor Gauss 和解析四阶余项界，四个非接触样本在原内部 relative target=1e-10、absolute target=0 下通过。不是调整科学阈值、不是以 Gauss 阶次差作证书，也不是完整 RZ self-gravity 签收。

科学清单 §§6–8、共享 CGS G、完整旋转环体源、point potential、原有 axis 连续解析及 near/contact 不 softening 的含义未改变。

## 可靠界依据

令 h(r,z)=r*K(d/s)/s=(1/4)∫[0,2pi] r/D dtheta。对沿任意单位坐标的位移，Newton generating function 的 Legendre 系数给出 |∂^m(1/D)|<=m!/d_min^(m+1)，因为 |P_m|<=1。

对整个非接触源矩形、所有 theta，d_min 是距离下界，r<=rh。Leibniz 法则于是给出：

- |h_rrrr| <= (pi/2)*4!*(rh/d_min^5+1/d_min^4)
- |h_zzzz| <= (pi/2)*4!*rh/d_min^5

区间长度 L 的两点 Gauss 误差上界为 L^5/4320 * max|fourth derivative|。Tensor rule 的权重全正、权重和为原区间长度，用 I_r I_z-Q_r Q_z 的两项 telescoping，无须猜测混合导数。最终源积分余项：

    area*pi/360 * ((dr/d_min)^4*(1+rh/d_min) + (dz/d_min)^4*(rh/d_min))

该式与生产 K 的数值误差独立。节点 +/-1/sqrt(3)、midpoint、width、kernel complementary root、K interval、权重、累计、pi、余项和最终 G/rho factor 全程 outward enclosure。没有依赖 libm cos 节点或以实际观测误差倒推预算。

Gauss 区间与原 Darboux 区间取交集；可靠界不相交明确失败，不能用后一个更松界掩盖。d_min=0、不可表示/溢出时不采用非接触 rule，继续既有接触上界或明确失败。源内/边界接触的可靠但较宽区间尚未满足严格目标，WorkLimit 未被改成成功。

## 样本、失败和工作量

源 r=[0.5,1]、z=[-0.23,0.71]、rho=1。observer 为 (2,0)、(0.75,2)、(0.01,2)、(2,-0.7)。CPU fixture 使用 G=1，独立 Decimal 诊断使用共享 CGS G，两组明确分列，不混淆单位或原生场。

原目标 1e-10 不变，最终分别使用 5761、2985、2377、4007 个子矩形。在首个样本中 maximum_boxes=4096 返回真实 WorkLimit；保留初次失败日志，并作为反例验证，采用内部8192上限后达到同一目标。算法预算在既有65536硬上限内，不是新用户配置或科学容差。

新增实际 kernel_enclosures 和 agm_iterations，累计包含已替换父矩形的计算，并贯通原生 face consumer。矩形计数不再被当作全部 kernel 工作；本四个全部非接触样本中实际每矩形10次 K interval 调用，AGM 迭代次数见 summary.json。计数不等于所有标量 FLOP，也不是性能 benchmark。

独立 C++ 24/32 阶源积分+256 angular points，以及 Python Decimal 12/16 阶源积分、128/256 angular points、80/100位诊断都落在本次区间内；工具不导入生产 K、导数界或矩计算。阶次/角向/精度变化用于参考稳定性诊断，不宣称 quadrature 余项被其差值证明。

## 构建、回归与身份

构建前 baseline：88746c86704e0bcb37feed0685323cf0f0375654，构建时本节点源码修改存在。完整 source hashes/scoped test ELF hash 见 summary.json，不能将测试产物写成未修改 baseline 的产物。

复用唯一 ARCH-compute-optim 工作区和既有 CPU Release build-cpu；standard incremental build ARCH、arch_composite_poisson、arch_self_gravity、arch_gravity_stage_contract，未 configure/迁移/新建树。memory guard 未停止，swap增长0。

最终通过：
- ring-separated-gauss（含4096-cap、零目标失败反例）
- ring-enclosure（接触/inside 仍真实拒绝）
- ring-native-face
- ring-far-leaf
- ring-axis-enclosure
- boundary-ledger
- gravity_stage_contract、composite_poisson_analytic、self_gravity_lifecycle、composite_poisson_contract，4项CTest
- architecture audit、diff check、独立 Decimal Newton 诊断

受影响构建后 ARCH ELF 仍为 d83186386a3dbcb403f473426da7dd42d1fc42840b09f7ddc43a7977fb36d95e；内部 gated path 在新 scoped test ELF 验证。生产 ARCH 无实际二进制变化，因此不重复跑匹配身份且已PASS的JENS 9短演化+9真实重启包。

复现：

    build-cpu/arch_composite_poisson ring-separated-gauss
    python3 validation/gravity/rz_ring_far_leaf_reference.py --probe build-cpu/arch_composite_poisson --separated --output <local-output.json>

## 发布与下一依赖

提交 shared source/math/consumer work counters、scoped tests、现有独立工具扩展、处理后标量摘要和报告。原始日志、H5、plt、checkpoint 留本机 studio/.local/integration/rz-separated-gauss-20261005 及既有 campaign。

本节点改善非接触可靠界；严格 contact/inside、general parent far/moment translation、完整 RHS coefficient/assembly/evaluation certificate、真实 AMR source publication 仍需贯通。角动量 A→B→C→D 科学签收与 RZ-VISC-01/RZ-AXIS-01 待审参考保持原范围。不开 production RZ、不开始对应 CUDA/长跑、不改 tag、不做 Windows 适配。
