# 有限环体轴解析：可靠算术账本节点

依据 Core 86bec324 科学清单第7.1–7.4节，接续219708b3。
本节点补齐 R_o=0 的解析分支区间，不替换为“小半径”近似或矩形求积，
生产 RZ gate 仍保留。

## 同一公式与区间

继续位于 GravityBoundary 原有 FiniteRingBoundaryMath。
采用现有 axis() 的四项表达：
u_hi*section(u_hi) - u_lo*section(u_lo)
+ r_hi²*Delta_asinh(r_hi) - r_lo²*Delta_asinh(r_lo)；
Phi=-pi*G*rho*以上结果。
section=(r_hi-r_lo)*(r_hi+r_lo) /
(hypot(r_hi,u)+hypot(r_lo,u))。
r_lo=0 的 r²*asinh(u/r) 采用连续零极限，不除零。

不新增源、solver、重力常数、EOS 或面力通道。
源仍为完整有限环体、常密度；返回观察点势，不是 cell/face 平均。
输入几何/G/密度以精确 stored FP64 为身份。

offset、加/减/乘/除、hypot、asinh 与最终求和全部传播区间：
hypot 使用已有缩放平方根包络；
asinh(x) 采用连续奇对称 log(abs(x)+hypot(1,x))；
log 下界是向下舍入的正 atanh 部分和，省略正尾；
上界继续保留已有48项及显式几何尾界。
log 双界按 binary exponent reduction，不依赖 std::log/asinh 的经验误差。
命名 shared host/device leaf 承担相同数学；CPU 实测，CUDA 尚未验收。

对于非负源，精确势非正，因此区间上端可与0取交集。
该操作仅收紧已知数学符号的误差区间，不裁剪科学 field、
不修改真实 potential/EOS/Config、也不制造正值修复。

result.bound_valid 指有有限可靠区间；
status=Bounded 还要求明确内部目标达到。
axis 不细分，因此 zero target 未达到或抵消误差过宽时
返回 PrecisionLimit，并保留有效诊断区间；
真正不可表示/溢出时 bound_valid=false。
无隐藏 atol floor、不把 estimate-only status 改成 certified。
zero source 沿既有精确0语义。

## 实际覆盖与未解决精度

独立 Decimal80/120 primitive 从3D Newton轴积分得到，
不用生产四项求值、距离/log区间或 kernel。
共享CGS G明确对齐 exact binary64，几何同样使用 exact float inputs。
13组源：r_lower=0/0.5，z=0/0.375/2/±100/1e6，
及相邻 binary64 边界的薄环体。
全部高精度参考被可靠区间包含；30个独立 Decimal log 双界检查PASS。

常规10组源满足显式内部 relative_target=1e-10；
这是数学接口 fixture 工作目标，不新增/放宽科学 residual 阈值。
极远轴两组与薄源一组明确 PrecisionLimit。
G=1诊断：r_lower=0.5,z=0的absolute error约4.2633e-14，
z=1e6约9.9614e-13，而 potential约-1.7671e-6；
后者达不到1e-10相对目标，不以旧 estimate 看似准确替代可靠界。
zero target、不可表示巨大坐标、axis无quadrature子域等反例PASS。
同一接口 leaf_boxes=range_evaluations=0：实际走解析四项，
log叶固定48项，没有Gauss节点或 source kernel evaluations。

当前 Delta_asinh 区间仍采用分别包络两端再相减，
远轴和很薄源有 dependency/cancellation overestimation。
这是真实算法限制；不能宣称所有 finite source 已达到严格 production budget。
后续须稳定差式/关联误差或已认证远树方案，保留失败传播。

原 non-axis/contact enclosure、ring/moment/双身份 guard、
original RHS acceptance PASS。
composite_poisson_analytic / composite_poisson_contract /
self_gravity_lifecycle 3/3 PASS；architecture、git diff --check PASS。
受控现有CPU Release build parallel28、无swap增长。
没有 configure、新build tree、simulation、Windows适配或科学长跑。

ARCH SHA256不变：
1917385417c8f366dc9834a9946744f55556405b6a4b5f48578efd14db803020。
production values 仍不调用新增interval接口；
不重复该ELF已通过的冻结JENS9+9，不改写Studio Build Manifest身份。

复现：
cmake --build build-cpu --target arch_composite_poisson --parallel 28
build-cpu/arch_composite_poisson ring-axis-enclosure
python3 validation/gravity/rz_ring_axis_enclosure_reference.py --probe build-cpu/arch_composite_poisson --output /tmp/rz-axis-enclosure.json

处理后指标/source identity：
validation/gravity/results/rz-axis-enclosure-20261004/summary.json。
raw/full logs仅studio/.local/integration/rz-axis-enclosure-20261004。
原始H5/plt/checkpoint、ELF、全量日志不提交。

## 当前清单与出口

- [x] exact-root K、distance/root、log/contact majorant与有限source范围。
- [x] axis使用同一解析公式，并传播可靠算术区间。
- [x] ordinary/zero-target/extreme/薄源精度状态可辨。
- [ ] 更紧的near/contact可靠积分达到实际budget。
- [ ] 稳定远轴/薄源区间与far support/moments/reduction误差。
- [ ] tree budget descent、source/AMR epoch、真实assembly/residual ledger贯通。
- [ ] 独立Phi/face-force科学门槛及RZ角动量全部消费者签收。

RZ-AXIS-01与RZ-VISC-01继续保留。
本数学节点可review，不代表科学Core完整签收。
CPU科学门槛后统一CUDA，再启动获准冻结长跑/计时。
Linux/WSL、唯一工作区、原始数据本机原则不变；整体目标未完成。
