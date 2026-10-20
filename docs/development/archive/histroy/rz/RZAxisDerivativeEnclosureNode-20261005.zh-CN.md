# RZ 环体轴线：导数余项独立包络节点

Core 86bec324018349c6d81df84a3cced3ed9f9a2792 科学清单第7节。
基线c2546a32fe4896a1fc283317d7c9b64bccc4c8a3；同一工作区、CPU Release、
dirtyAtBuild=true。本节点没有改物理定义、共享G、输入/验收阈值或RZ生产能力。
不是完整RZ/角动量/面力/CUDA/长跑科学签收。

## finding 与同一数学所有者

原四项轴线解析区间含大数相消。7f2f8e34节点z=1e6的区间真实过宽，
不能达到显式内部relative_target=1e-10；没有用estimate假装可靠界。
本次仍在GravityBoundary的FiniteRingBoundaryMath中保留该四项和失败机制，
增加一份独立有证明的轴线包络，与原包络取交集。
不另建源、solver、加速度通道、small-radius切换或用户积分精度参数。

源为exact stored FP64边界、密度与G所定义的完整有限环体，
观察点严格R_o=0。先精确积分r，令u=z-Z_o：

f(u) = sqrt(r_hi²+u²)-sqrt(r_lo²+u²)
     = integral r/sqrt(r²+u²) dr
     = (r_hi-r_lo)(r_hi+r_lo)/(hypot(r_hi,u)+hypot(r_lo,u))。

Phi = -2*pi*G*rho*integral_z_lo^z_hi f(z-Z_o) dz。

若d_min>0是整个源到轴点距离的可靠下界，
微分原积分给出
f''(u) = integral r*(2u²-r²)/(r²+u²)^(5/2) dr。
由|2u²-r²|<=2(r²+u²)及integral2r dr=r_hi²-r_lo²，
|f''| <= (r_hi²-r_lo²)/d_min³。
对精确几何中点Taylor余项积分，得到
|integral f dz - dz*f(midpoint)| <= dz³*(r_hi²-r_lo²)/(24*d_min³)。

这不是高斯阶数差或观察误差拟合；常数24来自
integral_-dz/2^dz/2 t²/2 dt。
offset、宽度、中点、径向section、乘除/G/pi全部沿现有区间叶传播；
余项采用因式分解dz*Delta_r²/d_min*(dz/d_min)²/24，
防止先立方大距离/极小宽度造成不必要overflow/underflow。
距离下界为0或独立计算不可表示时不使用这份包络，原解析区间仍保留。

两份包络均有限时取交集；区间不相交明确失败，不能“修复”为成功。
非负Newton被积函数只允许误差区间与数学符号取交，
不是裁剪科学field。原zero source/zero target/PrecisionLimit语义不变。
没有axis源子域细分或Gauss节点，leaf_boxes/range_evaluations仍为0；
独立包络包含一次径向已解析kernel的中点计算及严格余项，不能误称精确中点积分。

## 独立结果及真实精度限制

独立工具不导入生产kernel/区间：
Decimal80/120，从3D Newton轴积分primitive直接求源势。
exact binary64输入/G，31组源全部包含，30个log双界检查PASS。
包含r_lo=0/0.5、接触轴向位置、±100、±1e6、±1e12、±1e20；
另有非对称z=[-0.23,0.71]、正负远观察点，以及相邻FP64薄源。

30组普通/远轴源达到未变内部relative_target=1e-10：
r_lo=0.5,z=1e6，CGS G、rho=1时，
Phi约-1.179446166441439e-13 cm²/s²，
可靠absolute_error约5.93223400726201e-27 cm²/s²。
±1e12与±1e20同样通过；没有设置atol floor。
该接口fixture目标不是新物理residual门槛，不能换算成Phi/force科学签收。

相邻FP64 r=[1,nextafter(1,+inf)]、z观察点2仍PrecisionLimit：
absolute_error约4.71264e-25、value约-3.12322e-23（同CGS身份）。
它的有限区间保留，未声称薄源精度缺口已解决。
zero-target与不可表示最大坐标反例保留。

旧estimate不是独立truth，其远轴算术误差可能超过更紧区间。
fixture从“旧estimate点必须在新区间中”改为仅诊断其误差带是否相交；
这条不是可靠界证明或科学通过依据。真正包含性由独立Newton参考验证，
可靠性依据为上述导数界及逐步区间传播。

## 工程检查和身份

既有build-cpu内存受控增量编译ARCH及arch_composite_poisson，
parallel28，最低available约21.8GB，peak owned RSS约1.81GB，
swap0、guard未触发；没有configure、新build tree或源码副本。

axis fixture、31组Decimal参考、non-axis/contact范围、
原RHS boundary-acceptance与架构审计PASS。
composite_poisson_analytic、composite_poisson_contract、
self_gravity_lifecycle：3/3 PASS。diff check PASS。
这些检查不代表新应力数学、轴区局部norm或全面消费者签收。

ARCH SHA256仍
1917385417c8f366dc9834a9946744f55556405b6a4b5f48578efd14db803020。
生产值仍未调用此新包络；没有重复同ELF的JENS9+9，
没有制造新的Studio Build Manifest或把源码HEAD等同于binary来源。

复现：
cmake --build build-cpu --target ARCH arch_composite_poisson --parallel 28
build-cpu/arch_composite_poisson ring-axis-enclosure
python3 validation/gravity/rz_ring_axis_enclosure_reference.py \
  --probe build-cpu/arch_composite_poisson --output /tmp/rz-axis-derivative.json

处理后summary：
validation/gravity/results/rz-axis-derivative-enclosure-20261005/summary.json。
raw/full日志：studio/.local/integration/rz-axis-derivative-enclosure-20261005。
仅提交脚本、实现、标量摘要和报告；原H5/plt/checkpoint/ELF/全量日志留本机。

## 下一依赖

近场/contact更紧可靠积分、薄源、far树矩/FP64余项、tree budget descent、
source/AMR epoch、真实边界assembly/residual贯通仍待实施和科学材料。
角动量继续A→B→C→D；RZ-VISC-01及RZ-AXIS-01保留Core决定门槛。
CPU相关科学/消费链通过后统一CUDA，再开启已冻结长跑/计时。
本节点可独立review，不解除整体RZ能力门槛。
