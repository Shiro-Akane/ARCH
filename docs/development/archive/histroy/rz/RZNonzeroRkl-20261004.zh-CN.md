# RZ 非零实际 RKL 阶段证据

基线4efe5ce93ff095794c1e255e5e4c46caed0d1cb7。本次仅扩展实际Driver
Runtime fixture，不修改production科学公式。原始文件/ELF/日志本机保留，
提交fixture与处理后Summary。仍为工程证据，交Core review，不自定科学验收预算。

## 独立 quadratic 热参考

真实IdealGas使用e=Cv*T，shared thermal flux=-alpha*rho*Cv*grad(T)。
rho=2、Cv=3、alpha=.01；T=10+.2r²+.3z²，独立RZ数学：
(1/r) d_r(r d_r T)+d_zz T=4*.2+2*.3=1.4。
因此dE/dt=.084，dt=.9375时E增量=.07875。
参考直接由该解析式算出，不调用production diffusion operator取期望值。

实际Driver -> single RKL1两stages/RKL2五stages -> shared scheduler，
轴域/非轴域8组（direction标签在single场景是重复路由，不是不同网格）。
使用native cell-center quadratic输入；该有限差分/有限体积算子对quadratic
内部单元恰好给常量导数。检查区距非解析outflow边界至少stages个单元，
axis r=0使用实际Neumann标量镜像，故包括axis cell。
各案例checked168/66（axis）及144/36（非axis）；最大E误差
5.6843418860808015e-14。沿用fixture已有2e-12工程算术检查，未放宽。
活动增量约.07875，非零并非被跳过；质量/动量未改变。

这不是全域quadratic解析边界解，也不证明一般cell-average数据、
长期精度或物理场景验收。

## 非零 mixed AMR 能量收支

五叶mixed grid，radial/axial粗细接口及r_min0/1，RKL1/2共8组。
rho2/Cv3/alpha.01，T=10+.2cos(pi*(r-inner)/2)+.1cos(pi*z)。
真实outflow复制scalar导致外域边界thermal零梯度（轴Neumann亦零热通量）；
在该离散闭域中总热能应守恒。独立long-double求和采用
full-ring V=pi*(r_hi-r_lo)*(r_hi+r_lo)*dz，不调用Core CellVolume作为期望。
非零热传递和实际CF registration/reflux执行后相对能量误差最大
2.3826221883157601e-17；各案例最大active-cell变化.010255至.029661。
沿用2e-12归一算术门槛；此守恒检查不能代替场值accuracy/AMR收敛参考。

完整fixture同时复验原16组zero-viscous实际RKL检查，无新production修改，
未重复运行无变化的其他scoped检查。重编译fixture和三个Runtime TU，
链接已有CPU依赖archive，不冒充全production binary rebuild。
Driver counters time/step保持0，实际执行RKL kernels，无simulation driver循环，
无H5/plt/checkpoint。精确源码/头文件/ELF SHA及逐案例结果见Summary。

## 剩余目标

还需公共RZ配置/身份/IO/checkpoint统一语义、真实重网格角动量contract、
finite-ring近场gravity及JENS owner确认、CUDA、冻结O9场景/门槛与长期
benchmark/restart。当前nonzero thermal证据不把这些要求替换成较小目标。
