# RZ-B 共享 CPU Hydro torque 节点

依据 Core 86bec324 第8节，从 2eb4134a 的 RZ-A ghost节点继续。
本节点完成普通 patch hydro 的 J/W divergence、去重 source 和严格候选验证，
不是完整 B、完整 AMR angular evolution、RZ 科学签收或 CUDA qualification。

## 根因与改动

旧 explicit RZ helper仍以A/V推进m_phi，并保留-rho*v_r*v_phi/r。
新的唯一状态是m_phi=J/W；共享 scalar divergence 对其使用
dt*(T_lower F_phi_lower - T_upper F_phi_upper)/W。
T=integral r dA，轴r=0时精确0；GridMetrics提供所有测度。
其他流体、E、rhoX继续使用已有A/V，避免计算无用的m_phi V-divergence。
无第二权威J/ell数组、无第二flux或Riemann实现。

共享RZ geometric source仅去除已被torque divergence吸收的phi curvature。
径向压力/centrifugal保留；z、mass、energy不新增source。
legacy polar/full-cylinder source保持原公式，分别验收，不再要求RZ phi source与其相同。

RZ stage借用现有StateAdmissibility::validate，不用apply_bounds改变rho/J/E；
越过已配置floor/ceiling、未解析热能、非法组分明确拒绝候选。
旧RZ floor-repair fixture按照新批准contract改为明确拒绝且repair events=0。
非RZ stage原修复语义不变，物理门槛不放宽。

## 真实证据

含轴/非轴 × closed synthetic / open synthetic / actual HLLC PCM，共6组。
用独立full-ring多项式W与face torque积分、long double累加核对：
abs(delta J + outward torque impulse) /
(initial integral abs(m_phi) W + abs(outward impulse)) <=1e-12。
最大测得相对误差8.6803e-18；closed boundary impulse=0。
真实HLLC stage输出的J也按同一独立积分检查，repair=0。
mass/E/rhoX同时按V及独立边界area积分保持原收支。
这不替代D的rigid-rotation equilibrium空间收敛、强旋流及长时验收。

curvilinear_metrics、amr_flux_surface_plan、topology_transaction、
runtime_probe_and_capabilities、preview_api_contract、preview_session_contract、
self_gravity_lifecycle、source architecture、diff check全部PASS。
现有AMR/viscosity legacy fixture仅为兼容性回归；
未加入W torque的消费者不能凭这些PASS算完整科学通过。

## 共享调用失败与待审

首次改变共享divergence时，现有diffusion也消费了新测度，导致
RZ Host swirl linear operator失败；完整原日志留本机，不覆盖。
现在hydro显式启用torque，未迁移的viscosity owner保持原路径待同批迁移。
现有viscosity是vector-Laplacian，变量mu下与symmetric shear stress不同，
详见 RZViscousStressReview-20261004.zh-CN.md，finding RZ-VISC-01。
不猜测新的应力、work flux或独立参考；不将删除source视为足够修复。

## 身份与数据

既有CPU Release增量构建parallel28，memory guard PASS，无swap增长；
无configure/复制build tree/新工作区。
实际ARCH SHA256：0249942c26d084a8b502a10122d318bd46d9ec69cc07e0eab2d3aa8482d98028。
build时source HEAD=2eb4134adc8430628bc1735ff79b3654fb4c844a +本补丁，dirty=true；
delivery fingerprints见处理后summary。
未替换Studio managed binary或修改其Manifest。

新ELF的uniform-lifecycle-1九组短演化及九组真实checkpoint续算全部PASS，
mass/E drift=0、disabled/output-only原生状态与solve count一致。
不以旧ELF或CPU/CUDA一致替代独立科学参考。

处理后摘要：validation/amr/results/rz-hydro-torque-20261004/summary.json。
原H5/plt/checkpoint/ELF/全量日志留本机
studio/.local/integration/rz-hydro-torque-20261004/。

## 后续

B还需signed torque AMR registration/W reflux、axis/viscosity及全integrator预算；
C需Init/BC/EOS/diagnostics/plot/API/checkpoint版本与旧RZ拒绝；
D完整CPU科学后才统一CUDA和冻结长跑。
有限环体按已批准第7节继续，不把近场estimate当certified bound。
完整RZ能力保持gated，不开展Windows工作。
