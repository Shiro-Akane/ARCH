# RZ-B 开放边界 stage-weighted torque 预算节点

依据 Core 86bec324 第8节；接续6f2cdbb2。test-only，
不新增物理、不重建生产ARCH、不解除完整RZ gate。

## 实际 stage 与独立积分

沿用五叶 mixed-AMR 非零旋转初态，HLLC/PCM actual hydro owner，
radial/axial接口、inner radius0/1、Euler/RK2/RK3，各10步dt1e-4。
12组outflow开放边界；轴lower face仍reflecting。
同时保留并通过12组reflecting闭合边界，合计24组。

test observer只读每次真正evaluate_patch的state、dt、flux_weight；
所有更新与reflux仍委托actual owner。
boundary flux使用同一EOS/HLLC policy和同一mean/roe配置重新计算，
不复刻Riemann科学逻辑，不修改原state/response。
reference表面积、力矩测度独立long-double endpoint积分：
radial A=2*pi*r*dz,T=2*pi*r²*dz；
axial A=pi*(rhi²-rlo²),T=2*pi*(rhi³-rlo³)/3。
只计face_neighbors count=0的真实物理边界；
粗细内部面不计入外部账本，必须由真实AMR reflux守恒。
状态reference仍按独立V/W积分，不消费production metric helper。

每step累计 actual RK stage weight * dt * signed outward flux。
验收abs(deltaJ+outwardTorque)/(initial sum abs(m_phi)*W + abs(net outwardTorque))
<=原1e-12；分母使用net外流绝对值，不插floor，
在有正负抵消时比sum absolute individual impulses更严格。
rho/E/两组分各有独立同形式账本。
这是outward advective torque，不是新增applied body/viscous torque；
本场景gravity/diffusion均未启用。

## 防退化检查与结果

每组确认非零torque register；开放组确认nonzero outward torque。
每组检查observer收到5 leaves ×10steps×1/2/3 stages，
即50/100/150个真实patch-stage，不漏记RK阶段。

开放12组最大angular budget error=6.09833e-16；
mass=6.37925e-16，E=7.53772e-16，species=6.8489e-16；
所有repair counters=0。
closed12组outward torque精确0，原预算仍PASS。
累计开放torque约4.49386e-5至2.32432e-4。

独立negative control忽略RK stage weight，将同一真实stage flux
错误地每阶段全额累加。8组RK2/RK3错误预算均超过1e-12，
naive relative error约2.00488e-6至9.1698e-6；
测试明确要求其被拒绝，防止stage权重缺失也能“自证通过”。
Euler权重1不适用该反例。
没有改物理、引用数据、验收阈值或生产算法以通过检查。

curvilinear_metrics（含全部既有geometry/hydro/viscous兼容fixtures）、
amr_flux_surface_plan、source architecture、diff check全部PASS。
只增量编译arch_curvilinear_metrics，生产ARCH SHA256保持：
1917385417c8f366dc9834a9946744f55556405b6a4b5f48578efd14db803020。
同binary上一runtime节点的JENS 9+9证据不重复运行。

## 复现、交付与剩余范围

cmake --build build-cpu --target arch_curvilinear_metrics --parallel 28
OMP_NUM_THREADS=1 build-cpu/arch_curvilinear_metrics

处理后指标/source identity：
validation/amr/results/rz-open-torque-budget-20261004/summary.json。
完整日志本机studio/.local/integration/rz-open-torque-budget-20261004。
不上传H5/plt/checkpoint原数据、不进行Windows工作。

此节点仅关闭bounded inviscid stage-weighted boundary-flux预算。
axis空间收敛及>=1.8 equilibrium order、RZ-VISC-01 stress/work、
applied torque场景、C所有消费者与checkpoint版本语义、
finite-ring可靠near-bound/global ledger仍未完成。
完整CPU科学通过后统一CUDA，再执行已冻结对应长跑/计时。
