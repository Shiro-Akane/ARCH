# RZ-B hydro AMR torque registration / reflux 节点

依据 Core 86bec324 第8节，接续 f2c19db2。完成内部 opt-in RZ hydro
registration/reflux 与模式身份，不是完整 RZ-B/C/D 科学签收。

## 单一积分与消费者

m_phi=J/W；register MomW 为 signed torque/coarse ordinary face area。
Host AMRFluxRegistering 将 physical Riemann F_phi 乘 native source lever：
radial r_face、axial <r>_V。几何仅消费 GridMetrics，轴 lever精确0，
不构造0/0；无额外 J/ell 数组。
fine 使用原 A_f/A_c 权重乘 lever；coarse 原权重1乘 lever。
Host FluxRegister 保留全部字段同一 logical grouping，
仅 MomW coefficient乘独立 V/W factor，即 dt*A_c/W；
rho/m_r/m_z/E/rhoX 仍用 V correction。
Euler/RK2/RK3、hydro evaluate 明确 opt-in。

共享 AmrFluxExecutionPlan term/contribution 携带相同 factor；
CUDA stage scratch与initial operator surface、reflux MomW 使用同一数学。
CudaBackendResources传 authoritative topology，不猜坐标。
尚未CUDA编译/执行；CPU compiled metadata检查不代表CUDA签收。
Diffusion保持原模式；RZ-VISC-01仍待Core确认stress/energy work。
production RZ capability gate 未解除。

## 身份与失败传播

topology fingerprint version3纳入 angular_transport；
AMRControl缓存同时比较mode/chart/epoch/native geometry。
错误模式、chart、native bounds的累积register在correction前拒绝，
需显式Clear；缺native RZ snapshot、非法轴、非正/非有限V/W明确失败。
不改变物理、floor、repair、checkpoint或阈值。

## 独立证据

真实五叶mixed-level topology：
radial/axial × inner radius0/1 × stage1/-0.375 共8组。
非零phi flux走actual Host register/reflux；T/W reference来自独立
long-double full-ring endpoint积分，而非生产normalization函数。
最大relative angular budget error=2.62249e-18，原阈值1e-12。
rho/m_r/m_z/E/composition同时验证；compiled term lever和reflux factor
用独立积分逐项检查。原12组scalar normalization检查亦PASS。

现有ordinary mode fixture保留用于未迁移diffusion兼容；
curvilinear actual hydro与Euler/RK2/RK3 scheduled mixed-level fixture通过，
但既有fixture为zero-swirl compatibility，不能当作真实非零旋转
整段演化J预算或完整RZ科学签收。

7项CPU regression全PASS：
amr_flux_surface_plan、curvilinear_metrics、self_gravity_lifecycle、
preview_api_contract、preview_session_contract、preview_full_model_contract、
preview_mesh_geometry。source architecture、diff check PASS。
首次接口编辑误改generic group_fields参数导致编译拒绝，已修正；
summary生成曾误用output-only lane名，按真实output_only修正，
没有重跑或覆盖campaign、没有调整科学阈值。

## Build与冻结JENS

既有CPU Release增量parallel28、memory guard PASS：
min available13485056KiB，peak owned RSS10451240KiB，无swap增长。
无configure、新工作区或build tree复制。
source=f2c19db2+本补丁，dirty=true；
ARCH SHA256=1917385417c8f366dc9834a9946744f55556405b6a4b5f48578efd14db803020。

shared AMR owner修改后，在新ELF复验冻结uniform-lifecycle-1：
1D/2D/3D × disabled/output_only/active，9组演化至0.02和9组实际
0.01 checkpoint续算至0.02均PASS。native state/controller payload严格一致；
mass/E drift均0，disabled/output_only solve/stage solve count一致。
不将旧ELF证据转记新身份；不替换Studio managed binary/Manifest。

处理后摘要/source fingerprints：
validation/amr/results/rz-amr-torque-runtime-20261004/summary.json。
本机raw：studio/.local/integration/rz-amr-torque-runtime-20261004。
H5/plt/checkpoint、ELF、完整日志留本机。

## 未完成项

B：真实非零旋转mixed-grid全integrator预算、axis regularity/精度、
RZ-VISC-01批准后的统一stress/work/reflux。
C：Init/BC/EOS/sound/JENS/plot/API/checkpoint版本语义贯通。
D：完整CPU科学门槛后统一CUDA，再以冻结输入启动对应长跑和计时。
环体near/contact可靠bound、全局error ledger、production evaluator继续按第7节；
estimate仍不得放入certified列。
不声明完整RZ支持，不启动未批准的新RZ长跑，不开展Windows。
