# RZ 环体核、轴极限与接触估计节点

依据 Core 86bec324 第7节，接续1e584b76。
本节点完成CPU共享数学及有界独立检查，不是可靠近场界、production
boundary、完整RZ科学或CUDA签收。

## 唯一数学与算法

FiniteRingBoundaryMath属于既有GravityBoundary owner，只有一份Host/device公式。
求piecewise-constant full-ring的势点值，不改cell-center未知量，
不增加直接加速度或cell/face平均势路径。
Phi=-4G rho integral(r K(m)/s dr dz)，m=k²。
AGM入口明确接收sqrt(1-m)=d/s，d来自直接距离；
不计算易相消的1-4Rr/s²，也不先将极小positive complementary root平方至0。
K(0)精确pi/2，根为0明确SingularSample，不插epsilon或softening。

仅R_observer==0进入analytic axis primitive，a=0用连续解析式；
使用factored shell section、稳定log1p/asinh区间，未设置小半径阈值。
极小非零R仍走同一离轴核，独立3D参考检查通过。

inside/contact按observer所在原生子矩形切分，每个矩形用两个Duffy三角形：
x=x_o+t[(1-u)a+u b]，Jacobian=t*abs(det(a,b))；
Gauss采内部点。只有真实零边才几何消去，浮点underflow等不当作退化丢弃。
密度0数学源返回ExactZero，真实流体正密度契约未修改。

内部Gauss orders=4,8,16,32,64,128，
最大200000 kernel evaluations，每个AGM至多32次。
内部控制不是.par/schema/浏览器参数。
状态区分EstimatedConverged、AnalyticAxis、ExactZero、InvalidInput、
WorkLimit、PrecisionLimit、SingularSample、Nonfinite、RuleFailure。
返回实际kernel/AGM工作量、最后完成阶数、value及estimated_error。
到上限后保留最后完整结果，但明确失败，不静默当作收敛。

error_is_certified始终false。阶数差是积分估计，
轴解析式roundoff列也只是诊断，不冒充interval/arithmetic bound。
内部默认relative estimate target=1e-9；接触诊断1e-7只用于有界数学比较，
不是改变任何科学验收阈值或生产请求。默认interior在128阶返回WorkLimit，
174720 evaluations、G=1时estimate=2.038924984404389e-8；真实fixture断言该失败状态。
zero tolerance轴请求返回PrecisionLimit，不暗添floor。

## 独立证据

- complementary root=1/.5/1e-12/1e-100/1e-300：
  独立Decimal参数m参考用100–700位，不导入生产kernel；
  最大absolute K差5.684341886080802e-14，满足原数学工程ULP检查。
- 原有8个solid/hollow/edge/far/shifted轴样本：
  Decimal80与120位一致，matched source实际CGS probe最大相对差
  3.1561940050166586e-15。source轴零极限保持。
- 外部、近轴实际离轴sample对独立3D Newton product积分通过。
- outer face、corner、interior三个真实contact：
  独立Decimal AGM+Duffy，32/64阶、t区间1/2份、
  80/100位分别改变。最高配置精度binary64一致；
  分区差约1.0899e-15 cm²/s²；
  production与细分参考差约2.7106e-16 cm²/s²。
  实测evaluations为87360/43680/174720，
  AGM iterations为466038/226880/934888。
  这些是估计质量证据，不证明可靠误差上界，也不覆盖contact force。

既有offaxis exterior potential/force入口仍拒绝contact，
新增--contact-review只返回bounded potential review；
不能用旧工具冒充inside/contact通过。
独立参考没有导入生产科学kernel，optional probe只对比固定math test executable。
阶数、精度和分区变化分别登记，不用相同实现自证。

## 回归、身份与复现

现有arch_composite_poisson ring入口、相关composite_poisson_analytic/contract、
self_gravity_lifecycle、source architecture、diff check全PASS。
既有CPU Release增量构建和memory guard PASS，没有configure、新工作区、
build tree复制或仿真运行。
生产ARCH SHA256仍为651d3be57664ff2416e2fe1d997e34dc34146c9d77134ce58db48b1ebb990215；
新增math尚未接入受gate保护的runtime values，当前production行为不变。
build source HEAD=1e584b762b315526b28d73895d476f305020a452+本补丁，dirty=true。
没有改写Studio managed manifest或把旧JENS结果当作新身份结果。

复现（在唯一repo根目录，使用既有science venv）：
- build-cpu/arch_composite_poisson ring
- venv/bin/python validation/gravity/rz_ring_axis_reference.py --kernel-probe build-cpu/arch_composite_poisson --output <local-axis-json>
- venv/bin/python validation/gravity/rz_ring_offaxis_reference.py --contact-review --kernel-probe build-cpu/arch_composite_poisson --output <local-contact-json>

summary包含精确source/test fingerprints、binary身份及逐样本标量摘要：
validation/gravity/results/rz-ring-kernel-20261004/summary.json。
本机raw目录studio/.local/integration/rz-ring-kernel-20261004；
全量日志、参考中间物和ELF留本机，不上传H5/plt/checkpoint或完整场数组。

## 未完成出口

可靠near/contact quadrature界、全部FP64/distance/support/moment/归约rounding
ledger、tree error-budget descent、原始Poisson exact-ring RHS residual判据、
完整source/AMR epoch失效尚待贯通。GravityBoundary::values的双向RZ gate仍拒绝。
不将finite estimate放进certified residual列，不宣称Phi/face force或完整演化通过。
RZ-B reflux仍可推进；viscosity的RZ-VISC-01等待Core定义确认。
C/D与统一CUDA、冻结长跑继续按依赖进行；不开展Windows适配。
