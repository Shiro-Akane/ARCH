# RZ viscosity stress / angular-budget review finding

Finding ID: RZ-VISC-01
状态：待 Core 确认；不阻断已批准 hydro/AMR/ring 数学节点，完整 RZ gate 保留。
依据：86bec324 第8节要求同一既有应力进入 torque divergence，
只去除重复方位源项，数学／物理定义和验收门槛不能自行更换。

## 当前实现证据

DiffFlux::assemble_diffusion_face_flux 的径向 phi flux 是
F_phi = -mu * partial_r(v_phi)，mu=nu*rho；
evaluate_geometric_diffusion_cell 使用未解析 phi 基底连接两次，
给出 phi source = -mu*v_phi/r^2（FV inverse-radius 版本）。
代码明确描述为 vector-Laplacian，能量采用同一动量通量与 face velocity 的功。
对于变量 mu，它不是可以直接当作 symmetric shear stress 的实现。

RZ-B 首次仅对共享 divergence 切换 W/torque，curvilinear_metrics
在 RZ Host swirl linear operator 失败；完整失败日志保留本机
studio/.local/integration/rz-hydro-torque-20261004/geometry-full.log。
此失败不能用放宽旧阈值消除，证明 stress/source 必须同批迁移。
当前 hydro 显式启用 torque，diffusion 保持原 owner 路径待确认。

## 连续极限下的具体差异

现有 phi operator（忽略轴向项）：
L_old = (1/r) d_r(r mu d_r v_phi) - mu v_phi/r^2。

把当前 mu*d_r v_phi 原样放入 torque divergence 并删除原 phi source：
L_torque = (1/r^2) d_r(r^2 mu d_r v_phi)，
与 L_old 相差 mu*(d_r v_phi/r + v_phi/r^2)。
因此只去除 source 仍不能保持旧算子。

若改用 tau_rphi=mu*(d_r v_phi-v_phi/r)：
L_shear = (1/r^2) d_r(r^2 tau_rphi)
        = L_old - (d_r mu)*v_phi/r。
constant mu 时相同；variable mu 时存在明确差异。
刚性转动 v_phi=Omega*r 时 tau_rphi=0，而旧 variable-mu
operator=Omega*d_r mu；现有 RZ variable-mu fixture 的非零期望就是这一项。
这些差异是物理/应力定义问题，不是 tolerance 或网格微调。

同时需要确认相应 phi viscous work flux 的定义；
不能只替换 angular momentum stress 而沿用不匹配的 energy work。
其余径/轴应力、总能量、质量和组分项保持已批准定义。

## 给 Core 的精确问题

1. 第8节“既有应力”指当前 vector-Laplacian flux，
   还是明确批准 RZ 方位分量采用上述 symmetric tau_rphi？
2. 若批准 tau_rphi，请确认 variable-mu 刚性转动参考是否应从
   Omega*d_r mu 改为0，并给出 corresponding energy-work 参考与独立预算。
3. 单元代表 u_phi=m_phi/rho；face strain 使用 W-centroid 重构与 axis
   regular extension时，期望的近轴/强旋流 fixture、解域和收支是否另有冻结定义？
4. 是否保留当前非 RZ vector-Laplacian 全部语义，将此迁移限定为 explicit RZ？

候选改动仅在 Core 确认后实施，复用 DiffFlux 的 coefficient/EOS/stress owner、
同一 torque measure 与 signed AMR register；不建立第二 viscosity kernel、
不通过 floor/heating 改 J/E，不把 source-to-zero 当作足够的验收。

其余 hydro torque、reflux、finite-ring、C消费者可继续按批准依赖推进。
当前完整 RZ / CUDA / 新 RZ long-run 出口仍未解锁。
