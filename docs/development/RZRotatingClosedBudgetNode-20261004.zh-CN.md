# RZ-B 非零旋转 mixed-level closed-budget 节点

依据 Core 86bec324 第8节，基线00db0191。只新增有界科学验证，
不改 production algorithm，不解除 RZ gate。

## 场景与真实路径

五叶 mixed-level topology，两种接口方向（radial/axial），
inner radius=0/1；实际 HLLC/PCM + Euler/SSPRK2/SSPRK3，
每组10步 dt=1e-4，endpoint=0.001，共12组。
这是内部短数学/守恒 fixture，不是新增已签收 production model 长跑预算。

初值有非零、非均匀密度、r/z速度、角向速度和两组分；
角向初态使用 W centroid，轴满足 odd u_phi=O(r)。
E含正 internal energy及代表速度kinetic；
未要求该fixture是刚体旋转平衡，也不用于证明空间精度。
每个物理边界为reflecting，径向轴torque=0，
外部advective mass/E/torque通量为0。
实际 BC、shared AMR ghost transfer、scheduler、hydro stage、
signed register、W reflux、strict admissibility全部执行。

每一步从真实原生leaf数组按独立long-double endpoint积分：
V=pi*(rhi²-rlo²)*dz，W=2*pi*(rhi³-rlo³)*dz/3。
不用生产GridMetrics或归一化函数产生守恒reference。
角向误差为 abs(J(t)-J(0))/sum_initial(abs(m_phi)*W)；
质量/E/两组分分别使用本身初始正积分，无epsilon/floor。
原阈值1e-12，每步检查，所有repair counters保持0。
每组断言实际torque register非零，避免退化成zero-swirl/no-reflux兼容检查。

## 结果

12组PASS，最高angular relative drift=6.11502e-16；
最高mass drift=6.21547e-16，E drift=7.40694e-16，
species drift=6.67965e-16。
实际 max torque register范围约4.21575e-5至3.56563e-4，
说明粗细面确实消费了非零角向通量不平衡。
这些量是处理后FP64输出精度的标量摘要，原始完整日志本机保留。

初次多步测试遗漏step之间ghost生命周期，真实scheduler返回
state region is not readable，失败日志保留。
修复测试流程：每步使用实际 complete_boundary owner，
执行BC和AMR exchange后发布相同interior version的新completion。
没有直接伪造ledger状态，没有放宽科学阈值，
没有因失败修改production物理。

curvilinear_metrics全部既有兼容/geometry/hydro/viscous fixtures、
amr_flux_surface_plan、source architecture、diff check PASS。
只重编译现有test target，未重建production ARCH。
ARCH SHA256保持1917385417c8f366dc9834a9946744f55556405b6a4b5f48578efd14db803020。
上一节点同一生产binary的JENS 9+9证据不重复执行，
也不将本test-only节点记成新production build。

## 可复现与覆盖限制

在唯一Linux/WSL repo：
cmake --build build-cpu --target arch_curvilinear_metrics --parallel 28
OMP_NUM_THREADS=1 build-cpu/arch_curvilinear_metrics

处理后摘要：
validation/amr/results/rz-rotating-budget-20261004/summary.json。
原始日志本机：
studio/.local/integration/rz-rotating-budget-20261004。
无H5/plt/checkpoint原数据上传。

本节点关闭的是bounded closed-boundary nonzero-swirl mixed-AMR
Euler/RK2/RK3积分预算，不是完整RZ科学finding。
open/external torque预算、axis空间收敛及>=1.8 equilibrium order、
RZ-VISC-01应力/work确认、C全部消费者/checkpoint语义、
可靠finite-ring near-field bound和全局ledger仍未完成。
完整CPU科学通过后统一CUDA；尚未启动RZ长跑或Windows。
