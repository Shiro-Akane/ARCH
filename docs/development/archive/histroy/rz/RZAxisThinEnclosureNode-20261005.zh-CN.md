# RZ 有限环体轴线：薄源稳定差式与径向余项节点

依据Core 86bec324018349c6d81df84a3cced3ed9f9a2792科学清单第7节。
基线aeeca8e3ae517a87d36e6e2b652aecb5c2e1f922；唯一ARCH-compute-optim工作区，
CPU Release，sourceDirtyAtBuild=true。保持生产RZ gate及全部科学定义/预算。

## finding 与公式

上一节点远轴的径向解析积分包络解决已测远轴精度，
但相邻FP64径向源仍不能达到未变内部目标relative_target=1e-10。
本节点在同一GravityBoundary/FiniteRingBoundaryMath所有者内补另一份独立界，
不加用户旋钮、softening、小半径切换、独立solver或force路径。

先精确积分z，u=z-Z_o：
g(r)=integral r/sqrt(r²+u²) du
    = r*[asinh(u_hi/r)-asinh(u_lo/r)]，
Phi=-2*pi*G*rho*integral_r_lo^r_hi g(r) dr。

同侧且u_lo>=0时，精确差采用
log1p(dz*[1+(u_hi+u_lo)/(hyp_hi+hyp_lo)]/(u_lo+hyp_lo))；
因为hyp_hi-hyp_lo=dz*(u_hi+u_lo)/(hyp_hi+hyp_lo)。
负侧通过asinh奇对称镜像；跨零使用两侧正贡献，避免相消。
dz来自原source两端，不由两个observer offset相减制造新不确定性。

共享log1p区间在x<=1时用正atanh级数z=x/(2+x)，
复用48项及显式几何尾界。x>1使用现有log双界/binary exponent reduction。
前者不先形成1+x，因此1e-300或minsubnormal不会静默消失。
不依赖std::log/log1p经验精度；输入负值/不可表示区间明确拒绝或退回现有有效包络。

微分原积分：
g''(r)=integral -3*r*u²/(r²+u²)^(5/2) du，
故|g''|<=3*r_hi*dz/d_min³。
径向中点积分余项：
|integral g dr - dr*g(mid_r)| <= dr³*r_hi*dz/(8*d_min³)。
常数8来自中点Taylor积分24与上界系数3，
不是由观察误差拟合。计算因式分解，避免先立方造成不必要溢出。

精确mid_r可能不是stored double。区间包络真实中点，
利用asinh差=integral du/hypot(r,u)随正r单调递减，
在midpoint两端包络，再乘r/dr并加入严格余项。
d_min=0或不可表示时不使用该独立界，原路径/失败语义仍保留。

保留原四项解析区间及上一节点独立界，与本次径向界逐一取交。
发现任何两份可靠界不相交立即失败，后续候选不得覆盖错误变成功。
数学非负被积函数只收紧误差区间；不裁剪科学字段、补热或改变状态。

## 独立科学数学材料

独立3D Newton轴primitive（不导入生产kernel/区间/差式）：
40组源全部包含，原30组普通/远轴/非对称案例及10组相邻FP64薄源。
薄源r=[1,nextafter(1,+inf)]，observer z=0/0.375/±2/±1e6/±1e12/±1e20，
均达到未变内部relative_target=1e-10。

Decimal源参考提升至160/200位，原因是薄源+远轴的primitive相消需要更高参考精度；
未改变生产目标或科学预算。沿原独立pi常量及exact stored FP64 G/geometry。
另30组log双界和10组log1p独立参考PASS；极小log1p使用Decimal400/480，
确保1+minsubnormal在参考侧也未被精度舍弃。

薄源z=2、CGS G、rho=1：
value约-3.143796217173644e-23 cm²/s²，
absolute_error约3.87913135771355e-37，relative约1.23390e-14。
薄源10组最大relative interval radius约1.27367e-14。
这些是处理后实际指标，不用它们定义新阈值。
历史报告的薄源PrecisionLimit及宽区间原样保留；本次独立证据才更新该fixture状态。

zero-target依然PrecisionLimit；最大不可表示坐标bound_valid=false；
负log1p区间拒绝，non-axis/contact上限失败保持。
所有axis案例仍没有源子域细分/Gauss节点，boxes/range_evaluations=0；
但界使用有严格余项的中点积分计算，不声称中点结果本身精确。

## 工程验证、身份和复现

同一既有build-cpu增量编译ARCH及arch_composite_poisson，parallel28、
内存guard最低available 21,800,328KiB、peak owned RSS1,789,360KiB、
swap0且未触发guard。未configure、未新建或复制tree、未执行simulation。

axis fixture、40组独立源参考、log/log1p、non-axis/contact范围、
original RHS boundary-acceptance与架构审计PASS。
composite_poisson_analytic / composite_poisson_contract /
self_gravity_lifecycle 3/3 PASS；git diff --check PASS。
没有无相关变化重新跑Studio/npm或JENS冻结9+9。

ARCH SHA256不变：
1917385417c8f366dc9834a9946744f55556405b6a4b5f48578efd14db803020。
生产值尚未调用新增可靠区间，不能把数学fixture当作当前RZ binary已开放。
没有改写Studio Build Manifest或以新Git HEAD伪造ELF来源。

复现：
cmake --build build-cpu --target ARCH arch_composite_poisson --parallel 28
build-cpu/arch_composite_poisson ring-axis-enclosure
python3 validation/gravity/rz_ring_axis_enclosure_reference.py \
  --probe build-cpu/arch_composite_poisson --output /tmp/rz-axis-thin.json

处理后身份/标量结果：
validation/gravity/results/rz-axis-thin-enclosure-20261005/summary.json。
原始数据/全量日志只在studio/.local/integration/rz-axis-thin-enclosure-20261005。
仅提交实现、相关fixtures/参考脚本、处理后摘要和本报告，不上传H5/plt/checkpoint/ELF。

## 当前清单与出口

- [x] 现有K/log、距离和原始axis解析区间。
- [x] 已测远轴相消及相邻FP64薄源，严格余项与独立包含检查。
- [x] 精确中点不可表示、tiny log1p、zero目标/overflow失败可辨。
- [ ] non-axis/contact达到实际严格production budget。
- [ ] far-tree FP64/余项/归约、预算下树和source/AMR epoch。
- [ ] 原生边界assembly与original residual ledger实际贯通。
- [ ] Phi/face-force独立科学预算以及RZ角动量全消费者签收。

RZ按A→B→C→D继续；RZ-VISC-01、RZ-AXIS-01各自的Core待定规则不擅改。
CPU对应科学/全消费链通过后统一CUDA，再启动冻结长跑和计时。
本节点独立交review，不关闭完整RZ finding；整体目标仍未完成。
