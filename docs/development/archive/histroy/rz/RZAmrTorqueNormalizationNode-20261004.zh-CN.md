# RZ AMR torque normalization 与消费者审计节点

依据 Core 86bec324 第8节；接续 bff3b028。只完成共享标量数学与消费者审计，
不是完整 registration/reflux 迁移，也不关闭 RZ-B 科学 gate。

## 唯一状态与归一化

唯一角向槽仍为 m_phi=J/W，无额外 ell 或 J 数组。
沿用同一 signed fine-minus-coarse register：
MomW 将存储 torque integral / coarse ordinary face area。
fine contribution 的 ordinary A_f/A_c 权重乘以 T_f/A_f，
coarse contribution 的权重1乘以 T_c/A_c；
随后 ordinary dt*A_c/V correction 乘 V/W，即 dt*A_c/W。
因此角向积分增量为 dt*(sum T_f*F_f - T_c*F_c)，
方向符号与正、负 RK stage weight 保持原计划定义。
rho/m_r/m_z/E/rhoX 继续使用 ordinary V/A。

AmrFluxMath 增加两份 Host/device 标量变换入口，
不新增几何定义，不在其中推断 hydro/viscosity 模式。
native T、A、V、W 必须由 GridMetrics owner 提供并先验证。
轴径向面直接提供 lever=0，禁止先构造0/0。
本节点尚无运行时调用这些入口，不改变已 gate 的 RZ production 支持。

## 已核对职责与下一接线

- AmrFluxPlan/AMRControl：拓扑和 native geometry 快照、fingerprint、缓存 owner。
  下一节点必须把 angular transport identity 放入 cache/register identity，
  hydro 与尚未确认的 viscosity 不能混用。
- AMRFluxRegistering：Host 注册 raw physical face flux；下一节点仅 MomW
  做共享 lever 变换，不改变其他字段或 stage 权重。
- FluxRegister：Host grouped reflux；MomW 使用 V/W 归一化。
- AmrFluxExecutionPlan：group_fields 当前要求全部字段共用 ordinary weight。
  直接改 MomW logical weight 会形成 incomplete field group，不能这样绕过。
  compiled registration term / reflux contribution 应携带独立 angular factor。
- src/cuda/amr/AmrFluxSurfaceKernels.cuh：register_route_kernel 同时读取
  StageScratch 与 InitialOperatorCache，二者都必须使用同一 factor；
  reflux_kernel 的 MomW 同样需要独立 factor。
- src/cuda/runtime/control/CudaBackendResources.cpp：compiled plan lowering/upload owner，
  要从 authoritative native topology 提供上述 factor，而非 device 猜坐标。
- Euler/RK2/RK3 + TimeIntegratorHelper：hydro 显式选择新 torque 模式。
- DiffusionAMRStages：暂保留已有模式。RZ-VISC-01 需要 Core 明确 stress、
  variable viscosity reference 与 energy work 后再迁移，不擅自改物理。

不启用只有 Host 正确而 device 使用旧公式的 capability。
CPU 全科学通过后再统一 CUDA；本节点不宣称 CUDA 已通过。

## 独立检查

测试按 full-ring endpoint 积分独立构造 radial T=2*pi*r_face^2*dz、
axial T=2*pi*(rhi^3-rlo^3)/3，以及 V、W。
径向/轴向 × inner r=0/1 × stage=1/-0.375/0 共12组，
包含非均匀 fine flux、coarse flux、axial children 不同 lever 和 exact axis-zero。
原阈值1e-12，最大 relative angular budget error=3.12307e-17。
没有调整阈值、floor、heat、原场数组或 checkpoint 语义。
这是归一化积分检查，不替代真实 mixed-level hydro 演化 J 守恒。

existing arch_amr_flux_surface_plan 增量编译、全部现有 surface tests、
source architecture 与 diff check PASS。
首次新增测试有 newline quoting 编译错误，已修正并重新通过；
无科学阈值变更。日志留本机。
只重编译测试 executable，未重建生产 ARCH；现有 ELF SHA256
651d3be57664ff2416e2fe1d997e34dc34146c9d77134ce58db48b1ebb990215。
不重复已完成 JENS 短包，也不将旧结果转写成新 build 身份。

处理后摘要：
validation/amr/results/rz-amr-torque-normalization-20261004/summary.json。
raw 位于 studio/.local/integration/rz-amr-torque-normalization-20261004。
无 H5/plt/checkpoint/完整日志上传；保持唯一 Linux/WSL 工作区。

## 未完成项

运行时 registration/reflux、cache mode identity、真实 mixed-level 非零旋转
J budget、黏性应力确认、C 端所有消费者和 checkpoint 版本门槛仍待完成。
环体可靠 near-field bound、全局 error ledger 与 production evaluator 继续按依赖推进。
不启动尚未科学签收的 RZ 长跑，不开展 Windows。
