# RZ 有限环体矩与远场截断界节点

依据 Core 86bec324 第7节，从86cd83e8继续。该节点是完整finite-ring
production boundary的必要共享数学和tree缓存，不是完整O7/RZ科学签收。

## 实现与职责

复用GravityBoundary现有十个raw Cartesian moments与parent translation。
RZ叶是完整方位、piecewise-constant density环体，展开中心为(0,0,z_mid)：
M=rho*pi*(r_hi²-r_lo²)*dz；
Ixx=Iyy=M*(r_hi²+r_lo²)/4，Izz=M*dz²/12；
dipole及cross moments精确0。parent仍用原平移公式，不能假定其反演对称。
真实existing tree now缓存这些RZ矩；support是r_hi²+(dz/2)²，
不使用meridional rectangle half diagonal，不将r_cell当作点质量位置。

现有Newtonian quadrupole算式抽成同一共享leaf，保留legacy表达式运算顺序；
没有第二GravityBoundary、第二moment数组、solver或科学kernel。

quadrupole后Legendre余项界：
一般源 G*absolute_mass/R*q³/(1-q)；
已证明反演对称的均匀full-ring叶 G*absolute_mass/R*q⁴/(1-q²)，q=a/R<1。
signed manufactured source必须给integral |rho| dV，而非abs(net mass)。
返回Bounded/InvalidInput/NotSeparated/Overflow结构化状态；
positive scalar series上向nextafter，不更改线程全局舍入模式。

该返回值只覆盖truncation，不含计算distance/support、moment/evaluation/sum的全局
rounding ledger，绝不以此宣称生产total certified boundary bound。
parent对称性不根据文件名、几何或开角自动推断。

## Gate

constructor允许建立内部RZ geometry/moment cache；
真实GravityBoundary::values仍明确拒绝RZ，直到near/error ledger完成。
RZ缓存也不能作为legacy log-kernel边界求值；
legacy缓存不能用在RZ op。两个方向都有真实拒绝fixture。
API/production capability仍未开放，未运行新RZ simulation或长跑。

## 独立验证

现有arch_composite_poisson target扩展ring/rz入口，无新CI job。
含轴/非轴leaf的analytic mass/second moments与实际uniform/mixed AMR tree
parent mass/dipole/second moments符合独立多项式积分，更新density重新计算。
线性z密度的parent dipole非零，明确反例证明不能把parent当对称叶。

q=.01/.2/.7/.95：独立Gauss-Legendre24与32阶(r,z)，full azimuth256点，
分别积分真实1/|x-x'|，不是生产ring kernel或同一moment公式。
24↔32差<=1.41e-18，四例quadrupole实际误差均在冻结余项界内。
这一order difference只作far参考收敛证据，不是near certified bound。
净质量0的signed pair仍有真实误差3.4056046919303193e-4；
以absolute mass=1.1780972450961724得到general bound=.027512160007494232。
contact/inside-support不能当far，overflow不能变成PASS/零误差。

RZ manufactured operator、composite_poisson_analytic/contract、
self_gravity_lifecycle、真实source architecture和diff check均PASS。
原解析/收敛阈值未更改。
只有最终新增signed/cache反例的test unit重建和ring fixture复验；
未重复无新源码变化的全套检查。

## 身份与数据

既有CPU Release增量构建，无configure/clone/build-tree复制；memory guard PASS。
ARCH SHA256：651d3be57664ff2416e2fe1d997e34dc34146c9d77134ce58db48b1ebb990215。
build时source HEAD=86cd83e84c576c6c909cbcc8cb956dc730d5068c +本补丁，
dirty=true；最终source/test fingerprints见summary。
此前JENS冻结九通道证据仍关联上一节点0249942c... ELF，
本节点没有把旧JENS结果改写成新binary结果；periodic JENS path未修改。
未替换Studio managed binary或修改其Manifest。

processed summary：validation/gravity/results/rz-ring-moments-20261004/summary.json。
全量本地日志/ELF保留studio/.local/integration/rz-ring-moments-20261004；
不上传H5/plt/checkpoint或完整场数组。

## 后续

继续elliptic ring kernel（complementary parameter）、精确axis limit、
contact rectangle split + Duffy、明确work-limit/error状态、全局descent/near-bound
ledger与Poisson request budget；source/AMR epoch绑定同批完成。
近场estimate不冒充certified bound，尚未关闭完整production gate。
RZ-B torque reflux可继续；viscosity的RZ-VISC-01需Core确认。
C/D、统一CUDA及批准长跑仍待按依赖贯通，不开展Windows适配。
