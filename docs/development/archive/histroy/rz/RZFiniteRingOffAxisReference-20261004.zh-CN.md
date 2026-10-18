# RZ 有限环体离轴参考候选

日期：2026-10-04。源码基线 55765729a727a36189f59a8fe8f85ad591c3c1ad；
独立工具/测试输入 SHA 见同名 Summary。
fetch 后 compute/optim=8fc0dd25eefd2243e8c36f85440bac46994e2e73、
codex/o8-boundaries=23ff77c4f08419de2b3c5eadee214da2af25784e，科学审批条件无新增。
状态：independent-reference engineering-pass / physics-review-pending。
不代表 O7.4 生产实现或科学验收。

## 定义与独立性

源为均匀 rho 的有限 [rL,rR]×[zL,zR] 区域绕轴完整旋转体，
观察点为 (R,0,Z)，报告点值 Phi、g_r、g_z 与完整源质量。
不将薄环或点质量当作有限单元，不使用二维 log、镜像质量、softening 或 epsilon。
仅覆盖源外观察点：源内/接触点明确拒绝，不伪装奇异积分已解决。

先解析积分完整方位角，再对有限 r/z 体积做 Decimal Gauss-Legendre。
K/E 采用 AGM，参数 m=k²，公式来源：
[NIST DLMF 19.8.5–6](https://dlmf.nist.gov/19.8#E5)。
独立三维对照直接计算 Cartesian Newton 1/d 和向量/d³，使用
NumPy 的三轴节点；没有调用生产 GravityBoundary/GridMetrics 或 K/E 参考核。
两种路径都使用同一有限物理源，并记录有限源积分阶数，不能只凭一致性认定无限精度。

令 u=z'-Z，s²=(R+r')²+u²，d²=(R-r')²+u²，
m=4Rr'/s²；源体积微元为 r' dr' dz' dphi：
- 完整方位势核 I=4K(m)/s，Phi=-G rho ∫r'I dr'dz'。
- g_r=G rho ∫r'·2/(R s)·[E(m)(r'²-R²+u²)/d²-K(m)] dr'dz'。
- g_z=G rho ∫r'·4u E(m)/(s d²) dr'dz'。
- R=0 使用解析轴线极限，不使用小 R 截断；该路径已对轴线闭式参考核对。
- 均匀源正中平面的 g_z=0 来自精确反射对称性，明确按身份处理，非数值 epsilon。

单位：Phi cm²/s²，g cm/s²，质量 g；G=6.67430e-8 CGS。
结果是 observer 点值，不是 cell-centered 体积平均或 face average。
生产离散语义仍需 Core 决定，不能据工具偷偷选定。

## 有界实验及实际 finding

六例：实心环体离轴中面、上下对称点、空心源内腔、近外缘、远场。
整体源采用固定16/32/64阶，分别以60/100位算术计算；另用固定2×2源体分区、
每部分64阶对照。direct Newton 使用r/z 8/16/32/64及phi 16/32/64/128阶。
没有根据通过结果调科学阈值或自适应追到指定“pass”。

六例所有固定阶数/分区的60/100位结果舍入为相同binary64。
这证明这些算术精度间一致，不证明源积分误差为零。
除近外缘例外，整体64阶与2×2分区64阶也舍入到相同binary64。

近外缘 R=2.125 cm，源r=[0,2]、z=[-1,1] cm、rho=3 g/cm³：
- 整体32→64阶：Phi变化1.7766340097564158e-13 cm²/s²；
  g_r变化1.144853567424669e-11 cm/s²。
- 整体64→分区64：Phi变化1.5435481397815115e-17 cm²/s²；
  g_r变化1.9753441487098356e-15 cm/s²。
- direct Newton整体64阶对分区参考：g_r差1.975294809040658e-15 cm/s²。
  两个整体积分共享有限r/z阶数，直接三维与环核一致不能排除源积分误差。
因此不能称整体64阶近场足够，也不由这些数据倒推新的生产容差。

## 完整环体远场矩候选

相对源质心 zc=(zL+zR)/2：
M=pi rho (rR²-rL²) Delta_z；
Ixx=Iyy=M(rR²+rL²)/4；Izz=M Delta_z²/12；
Qzz=2(Izz-Ixx)，Qxx=Qyy=-Qzz/2。
保留3D Newton单极/无迹四极展开的势及其负梯度。
仅在观察点位于源包围球外给出展开诊断；这是适用域检查，不是生产opening策略。

远场(R,Z)=(30,40) cm：
- 单极Phi绝对误差1.2346674096677038e-11 cm²/s²；
  加四极后7.558246644870839e-16 cm²/s²。
- 加四极后g_r/g_z绝对误差4.5850382298617296e-17 /
  1.2913622859818259e-16 cm/s²。
这是一个候选误差样本，不足以冻结阶数、开角、误差分配或AMR aggregate策略。

## 检查与复现

8项检查通过：K/E已知值与m约定、Gauss多项式矩、轴线闭式一致、
密度/平移/上下反射/内腔力方向、g=-grad Phi、源分区质量/对称性、
完整体积四极无迹/梯度、非法与接触输入拒绝。
检查中的差分步长/算术比较仅用于独立工具恒等式，不作为生产科学门槛。

在仓库根目录执行：
~~~bash
python3 tests/tooling/validation/test_rz_ring_offaxis_reference.py -v
# 参考生成需要 numpy，使用现有 validation venv：
studio/.local/integration/physical-endpoint-runner-20261003/venv/bin/python \
  validation/gravity/rz_ring_offaxis_reference.py --output /tmp/rz-offaxis-summary.json
~~~
本机参考生成17.98秒，8项检查0.41秒；仅工具耗时，不是ARCH或生产kernel性能。
git diff --check通过。本轮仅工具/报告变化，不重复Studio或ARCH基线编译/测试。
完整日志留studio/.local/integration/rz-offaxis-reference-20261004。
提交精简标量参考/误差/脚本，没有H5、plt、checkpoint或完整场数组。

## 生产接线前仍需确认

现有GravityBoundary.cpp的unit_cell_moments、density update、parent组合与tree所有权应复用；
当前dimension2仍调用isolated_log_potential，不能以本工具给旧分支改名成RZ。
本工具不修改这些所有者，也不开放能力。

Core需定案：有限源piecewise-constant及势/面力点值或平均值、源内/接触积分路线、
近/远切换与阶数、边界截断/源积分/离散/AMR/MG/后端分项预算；
并给出球对称映射和源内独立参考、geometry/checkpoint身份拒绝策略。
拿到这些决定后才接生产环体边界；先前O7_RZ_IMPLEMENTATION_MAP的整条迁移清单仍有效。
O7.1一般EOS/父态规则、RZ完整CPU路径、CUDA及冻结终点O9均未完成。
