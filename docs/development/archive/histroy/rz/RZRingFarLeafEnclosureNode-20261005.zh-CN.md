# RZ 单叶远场区间节点（2026-10-05）

## 结论

SCOPED CPU PASS；production RZ capability gate 保留。实现科学清单 §7.1 已批准的单叶二阶展开与可靠尾界，源为完整、常密度的有限环体，不是点质量、薄环或二维 log。没有改变物理定义、原有预算或用户配置。

## 数学与职责

沿既有 FiniteRingBoundaryMath owner，单叶真实中心为 (0,0,(zl+zh)/2)，支持球 a=sqrt(rh²+(dz/2)²)。只有可靠距离下界 R_lower>a_upper 才能尝试展开。

M、Ixx=Iyy、Izz 使用第7节已有公式；这些是瞬时 interval evaluation，不另存一套权威 moments/cache。单叶完整旋转体且轴向关于中心对称，所有奇阶 Legendre 积分为零。|P_n|<=1，非负源的 n>=4 偶次尾项逐项不超过 GM/R*q^n，几何求和给出 GM/R*q^4/(1-q²)。

势截断项为 -G*(M/R+(3*nᵀI*n-tr(I))/(2*R³))。几何 midpoint、距离、pi、质量、矩、方向、截断项、尾项及最终归约都使用现有共享 outward interval 运算；尾界并非唯一误差来源。R inverse 形式避免不必要的 R^5 overflow。无法表达可靠界时保留旧源积分路径。

一般父节点不具有该单叶反演对称性，仍不得使用这个偶次尾界；尚未完成父节点 moment/translation arithmetic certificate 和树接受链。

finite_ring_potential_enclosure 只在整项误差满足请求预算时返回单叶远场结果。否则仍走现有源矩形细分/明确失败，不以几何 opening 或实际样本误差代替证书。axis=0 仍走既有连续解析分支；源内/接触未使用远场；没有 softening、radius threshold、epsilon、绝对精度 floor 或用户精度参数。

## 检查与独立参考

已有 build-cpu/arch_composite_poisson 增加 ring-far-leaf CLI。18 个样本：rl=0/0.5、R 尺度 1e3/1e6/1e12、径向/正负斜向，非对称 z 边界 [-0.23,0.71]，确保实际中心不误置零。

这些样本都在 maximum_boxes=1、原内部 relative target=1e-10 下通过，未执行 rectangle range subdivision。C++ full-azimuth Newton 8/12 阶诊断，以及独立 Python Decimal 12 阶80位/16阶100位全三维 Newton 诊断全部落在区间内；工具不导入生产 K、矩或尾界公式。阶次差和精度差仅诊断，不能称为已证明的 quadrature error 或完整物理参考收敛验收。

反例：完整支持球内 observer 不采用展开；零目标非零源不静默通过；接触点仍返回 WorkLimit。ring-enclosure、ring-native-face、ring-axis-enclosure、boundary-ledger 回归通过。4 项 CTest 与 architecture audit PASS；diff check PASS。

原生面消费者的工程 absolute target=1e-5 cm²/s² 不变；某些叶源现可提前返回符合该预算的 far interval，导致 boundary-only RHS ledger 数值与上一节点不同，新的四行数值单独保存，不覆盖旧记录，也不称为生产收敛。势误差与 RHS norm error（s^-2）仍分列。

复现：

    build-cpu/arch_composite_poisson ring-far-leaf
    python3 validation/gravity/rz_ring_far_leaf_reference.py --probe build-cpu/arch_composite_poisson --output <local-output.json>

## 构建与身份

构建前 baseline：2264b5f47bbb450648e277d05a9754183a63f3d3；实际存在本节点修改，文件 SHA-256 和 scoped test ELF hash 见 summary.json。

复用唯一工作区与现有 CPU Release tree，标准增量构建 ARCH、arch_composite_poisson、arch_self_gravity、arch_gravity_stage_contract，无独立 configure。内存保护未停止，swap 增长0。

受影响目标构建后 production ARCH ELF SHA-256 仍为 d83186386a3dbcb403f473426da7dd42d1fc42840b09f7ddc43a7977fb36d95e。新内部消费路径由本次 scoped test ELF 验证，不伪装为已启用的 production RZ。匹配同一 ARCH ELF 的上一节点 JENS 9 演化+9真实重启 PASS receipt 保留；本节点没有二进制变化或新的 JENS 证据，因此没有重复跑冻结 campaign。

上一节点报告中的测试路径已由 bin/arch_composite_poisson 修正为 build-cpu 下的 ./arch_composite_poisson，仅修正复现命令，旧测量身份与结果未改变。本机首次调用错误路径导致 FileNotFoundError，定位实际 ELF 后执行检查，未修改构建配置。

## 留存与未完成项

标量 summary.json、独立参考脚本和本报告供 review。原始输出、日志、H5、plt、checkpoint 留在 studio/.local/integration/rz-ring-far-leaf-20261005 及既有本机 campaign，不上传。

严格近场/接触积分预算、一般父节点认证、实际 AMR source-publication、完整原始 RHS/residual 门槛仍需实施。RZ-VISC-01/RZ-AXIS-01 未决参考保持开放；RZ A→B→C→D 和 CPU 科学签收后才统一 CUDA，并在对应预算冻结后启动长跑。不修改 tag、不进入 Windows 适配。
