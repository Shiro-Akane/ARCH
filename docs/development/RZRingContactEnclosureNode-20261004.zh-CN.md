# 有限环体：非轴线接触源的可靠范围积分节点

依据 Core 86bec324 科学清单第7.2–7.4节，接续 cad3c614 的 K 区间。
本节点首次为完整非轴线源矩形返回含接触输入的可靠势区间，
但没有达到严格生产精度，不能解除 RZ gate。

## 数学与所有权

实现继续位于 GravityBoundary 的 FiniteRingBoundaryMath。
物理源仍为 piecewise-constant full ring，observer 点势，
Phi = -4 G rho integral(r K(d/s)/s dr dz)。
不新增重力常数、softening、用户积分参数、solver、物理源或 force API。

离开接触的矩形用原生 stored bounds 的距离/补参数区间，
K 的单调性与既有 AGM 区间构造正 kernel 的逐域下/上界，
再乘可靠 area 区间。
这是范围求积，不使用 Gauss 阶数差作为误差界。
d/s 直接按距离构造；数学 d<=s 允许补参数上端取 min(1,q_upper)，
不是给物理参数加 epsilon 或消掉负值。

接触子域采用可積 majorant：
D(theta)=sqrt(q² cos²(theta)+sin²(theta)) >= max(q,sin(theta))
>= max(q,2 theta/pi)，故 K <= pi/2*(1+log(1/q))。
又 r/s<=1、s<=S，按观察点将矩形分成至多四个象限。
每个 a×b 象限取较长方向 extent=max(a,b)，用 d>=该方向绝对偏移：
integral kernel <= area*pi/2*(2+log(S/extent))。
这是对数奇性的有限解析上界，不采奇点、不截断场、不加 softening。
接触子域下界保守取0，误差账本不利用符号抵消。

正 log 的上界使用 binary exponent reduction 和
log(m)=2*sum(z^(2k+1)/(2k+1))，z=(m-1)/(m+1)。
48项后保留显式几何尾界 2*z^97/(97*(1-z²))；
ln2 也按该式包络。不假定 std::log 的隐含误差即可构造 certificate。
pi 及基本运算均向外舍入，sqrt 使用缩放避免不必要的平方溢出。
同上一节点，可靠性依赖 IEEE binary64/gradual underflow/
基本运算及 sqrt 舍入的 project build contract；本次仅验证 CPU。

R_o=0 严格保留既有轴解析分支。
该分支 reliable arithmetic 尚未完成，新 enclosure 在非零源轴输入
明确返回 PrecisionLimit、bound_valid=false，不能改用矩形求积替代契约。
零源是精确0，不产生容差 floor。

## 工作与失败语义

共享数学叶函数 host/device 相同；自适应资源控制器此时仅 CPU。
算法在最大宽度子域二分，并重新向外归约完整区间。
value 为有限区间中点；absolute_error 包住至两端的距离。
Bounded 表示明确内部 absolute/relative target 已满足；
WorkLimit 表示没有满足目标，即使仍有可靠、但很宽的诊断区间。
bound_valid 不是 convergence，调用方不得把 WorkLimit 当成功。
extent 不合法、负源、非有限值、零资源预算拒绝；
溢出/不能表达 midpoint 返回 PrecisionLimit，不 fallback。

内部 maximum_boxes 有上限65536，不进 schema/.par/browser。
本次16/128子域，range_evaluations=31/255；
完整 sum/rank scans 约 O(N²)，没有声称适合巨大生产预算。
未增加外部依赖、CI 矩阵或新 build tree。

## 真实证据与限制

独立 Decimal 参考不导入生产求积：
复用已审阅 full-ring exterior 与 contact Duffy 参考，
16阶60位、32阶80位，5组 exact stored source：
exterior radial、exterior axial、outer face、outer corner、interior。
两阶结果均被16/128子域区间包含；参考求积差本身仍不是认证界。
6组独立 Decimal log 样本覆盖 near1、幂2、1e100、最大double，
均被 log 上界包住。
G 对齐共享 literal 的 exact binary64，不用 decimal/binary 身份差伪装 source error。

G=1 的128子域诊断 absolute_error 约：
exterior radial0.06337、axial0.05534、outer face0.21651、
corner0.17625、interior0.32598；都比16子域收窄。
这说明第一个可靠源界已实现，但依然很粗，绝不代表生产1e-10目标满足。
所有 zero-target 非零源检查实际为 WorkLimit，不能算 scientific convergence。
显式宽松算法目标仅用于验证状态转换，未修改冻结科学输入或门槛。

负密度、零预算、axis pending、overflow、不可表示细分、
exact zero、严格目标失败反例 PASS。
ring/contact/双身份 guard、K interval、original RHS acceptance PASS。
composite_poisson_analytic / composite_poisson_contract / self_gravity_lifecycle 3/3 PASS。
architecture、git diff --check PASS；受控现有 CPU Release build parallel28，
memory guard PASS、无 swap 增长。

ARCH SHA256 不变：
1917385417c8f366dc9834a9946744f55556405b6a4b5f48578efd14db803020。
新接口仍未被 production values 调用，不重复该 ELF 冻结 JENS9+9，
不伪造 Studio Build Manifest freshness。

复现：
cmake --build build-cpu --target arch_composite_poisson --parallel 28
build-cpu/arch_composite_poisson ring-enclosure
python3 validation/gravity/rz_ring_enclosure_reference.py --probe build-cpu/arch_composite_poisson --output /tmp/rz-ring-enclosure.json

处理后证据与 source fingerprints：
validation/gravity/results/rz-ring-enclosure-20261004/summary.json。
完整日志仅 studio/.local/integration/rz-ring-enclosure-20261004。
原始 H5/plt/checkpoint、ELF、全量日志不提交。

## 下一步与保留清单

- [x] Exact-root K 包络、distance/root 正范围、logarithmic contact majorant。
- [x] 完整非轴线 source rectangle enclosure 与清楚的工作/精度失败。
- [ ] 更紧的高阶/奇性分离可靠积分界，达到实际 residual budget。
- [ ] 轴解析公式的完整可靠算术。
- [ ] far moments/support/reduction、tree budget descent、source/AMR epoch。
- [ ] 真实 RHS assembly/residual arithmetic 与既有 Tsafe owner 贯通。
- [ ] 独立边界 Phi、Poisson Phi/face force 科学门槛。
- [ ] RZ-AXIS-01 / RZ-VISC-01 及完整角动量消费者签收。

原 estimate-only Duffy 路径保留、error_is_certified=false。
下一步不能仅增加 range boxes 直到一次结果看起来够好；
需要更紧且可证明的积分余项或奇性分离设计，保留失败传播。
CPU 完整科学门槛通过后再统一 CUDA，并执行对应已冻结长跑与计时。
不开展 Windows，整体目标仍未完成。
