# RZ C：Plotfile 角向表示消费节点

本节点依据科学清单第8.1/8.4节，从 d80ceafea671229b7ed25c7d39062f667d18efc2
继续唯一 Linux/WSL 工作区。仅补既有 writer 的角向表示、单位和测度可辨识性；
不改变演化数学、科学阈值或生产能力门槛，不开放 RZ Viewer。

## 候选文件映射

| 路径 | 数值/平均语义 | 单位/排列 |
| --- | --- | --- |
| NativeGrid/cell_measure | 原生 V，完整绕转 | cm^3，展平 Data 同序 |
| NativeGrid/angular_measure | 共享 GridMetrics 的 W=∫r dV | cm^4，完整绕转，展平 Data 同序 |
| NativeState/m_phi | 原 mom_w bits，J_cell/W；唯一演化表示 | g/(cm^2*s)，[block,x2,x1] |
| NativeState/angular_momentum_density | 共享转换派生 m_phi*W/V；不新增演化数组 | g/(cm*s)，[block,x2,x1] |
| Data/VELZ | 代表速度 m_phi/rho，非子单元速度体积平均 | cm/s，r-z-phi 正交基 |
| Data/DENS、ENER | 原生体积平均 | 保留 CGS 和原 bits |
| PRES、TEMP、JENS | 代表守恒态经既有 EOS/声速计算 | 不冒充真实场平均 |

x1-fastest、leaf interiors、无 ghost。NativeGrid 为 candidate-axisymmetric-rz-2；
root geometry revision=2/state_semantics=rz-m-phi-j-over-w-v1；
NativeState 为 candidate-rz-angular-1。名称/布局作为实际候选供 Core review，
没有扩展用户 plt_variables 或新增公共字段键。仍与旧 RZ candidate-1 不混用。

producer 复制真实同步 host state，以唯一 GridMetrics 权矩和 FluidState 转换派生，
serializer 做长度/有限性/正 W/J/V 一致性检查，checked-close 后 atomic replace。
不会归一化、夹紧、修改原始状态或 checkpoint；existing Cartesian 保持原路径。

## 实际验证

- 现有 CPU Release 增量构建 ARCH 和 arch_plotfile_publication 通过；未 configure。
  parallel28，峰值 RSS3328544 KiB，swap 增长0。
- publication scoped test 通过；10个缺失/长度/非法W/非有限态/错误J/V/错误几何反例
  均拒绝，原已发布文件 SHA 不变。
- 真实 DriverIO fixture 通过：不进入 timestep；HDF5 blocked-parent 诊断属于预期
  写失败传播反例，不是被隐藏的失败。
- 256-cell RZ 原生读回：W 对独立80位Decimal参考相对误差
  2.1370656127418904e-16；J/V最大绝对误差1.1102230246251565e-16。
  raw m_phi、VELZ 原值不变；原有2e-12内部工程门槛未改变。
- 此 IO fixture 直接指定 raw m_phi=2*(0.3*r_mid)，不是新的旋转 Init 科学参考，
  不用它证明 W 平均初始化、亚单元动能闭合或至少1.8空间收敛。
- Cartesian reader 接受原Cartesian候选；内部RZ明确拒绝
  WORKER_FAILED / Unsupported candidate native geometry。读取前后文件 SHA不变，
  没有把不支持状态伪装成 Inspector 可用。
- 86项现有 Plotfile/Host/显示映射回归通过，fail/skipped=0；architecture、diff check通过。
- 新实际 CPU ELF SHA256：
  b0f38076e245eb78c335b2324930f16d46cd952304aec5909ac100fe922812ad。
  此binary匹配的 uniform-lifecycle-1 冻结9短演化+9真实续算全通过。
  终点0.02、分割checkpoint0.01不变；质量/能量漂移0，最大JENS相对误差
  4.50750606346323e-17。原阈值/输入未变，9个restart均精确一致。
  guard53.135s，peak RSS818764KiB，无swap；这不是长跑或CUDA计时。

## 保留门槛与下步

仅交付 C Plotfile 的表示消费子项；不宣布 C/D 整体关闭。
Init/BC、其他 API 消费和 repair 账本仍需逐项核对，旋转独立收敛、
RZ-VISC-01、RZ-AXIS-01及完整环体 source/geometry/RHS certificate继续待审。
production RZ gate 保留；CPU 科学子组签收后才统一 CUDA 和对应已冻结长跑。

处理后证据：validation/io/results/rz-angular-plot-20261005/summary.json。
原始 H5、checkpoint、ELF和全量日志仅保存在
/home/arch/projects/ARCH-compute-optim/studio/.local/integration/rz-angular-plot-20261005。
没有 Windows 适配、main merge、新 workspace 或新 tag。
