# RZ 有限环体轴线独立参考候选

基线 e34c54f7405a6d9e2d8c481e32d4c57e782625c1。
fetch后compute/optim仍8fc0dd25，o8-boundaries仍23ff77c4，无新的科学裁定。
按JeansRZPlatformHandoff §4.3允许的有界实验/独立工具推进，
不改生产RZ路径、近场算法或能力目录。

## 数学与语义

均匀密度rho的有限源 r∈[rL,rR], z'∈[zL,zR] 绕轴旋转完整2*pi。
轴线观察点z，u=z'-z；此处是势/力点值，不是体积平均。

从三维Newton势核 -G*rho/distance 与 dV=r dr dphi dz' 积分：
H(u)=sqrt(rR²+u²)-sqrt(rL²+u²)
Phi=-2*pi*G*rho*integral H(u) du
g_z=-2*pi*G*rho*[H(zR-z)-H(zL-z)]
mass=pi*rho*(rR²-rL²)*(zR-zL)，g_r=0。

sqrt(a²+u²)的原函数为
F_a(u)=[u*sqrt(a²+u²)+a²*asinh(u/a)]/2；
a=0使用连续极限 F_0(u)=u*abs(u)/2。
源内部/边缘的轴线势与力由同一原函数给出，无epsilon或软化。
这不是薄环或二维log核，不把有限源替换为单个点质量。

validation/gravity/rz_ring_axis_reference.py 的Decimal 80/120位计算
使用高精度pi与已发布CGS G；不调用生产GridMetrics/GravityBoundary/EOS。
8个实心/空心、中平面、边缘、上下方、远处和平移输入，两精度舍入到相同FP64。

另一条独立路径直接对有限r-z源作8/16/32/64阶tensor Gauss-Legendre积分：
每个样本用三维Newton距离、完整2*pi*r Jacobian和原始density。
轴线上核与phi无关，方位积分可精确消去。它只是有界对照，不选择生产阶数。

## 结果与具体 finding

5项scoped测试通过：精度收敛、轴向力对称/方向、中平面对称、
轴向平移、density线性、有限体积partition、非法输入及源内/边缘有限值。
partition的14位算术检查仅属工具自洽，不是生产离散/科学误差预算。

64阶积分的空心/外部示例接近roundoff；源内/边缘明显较慢：

- 实心中平面 potential绝对差 2.4877116347186062e-10 cm²/s²，
  相对差 6.2557636902570957e-05。
- 实心源边缘 g_z绝对差 4.8376072065012167e-10 cm/s²，
  相对差 0.00032821199402888055。

不能因远处/空心例roundoff小，就认为固定tensor quadrature对近源也足够。
这份结果供Core选择有限源近场路线及预算，没有“科学PASS”或生产阶数推荐。

## 复现与交付

使用现有本机NumPy环境：
python -m unittest discover -s tests/tooling/validation -p test_rz_ring_axis_reference.py -v
python validation/gravity/rz_ring_axis_reference.py --output <local summary.json>

全阶数值和处理后绝对/相对差见同名Summary；日志保留本机
studio/.local/integration/rz-axis-reference-20261004。
未运行ARCH、build、simulation/CUDA；无原始科学文件输出/上传。
无Studio改动，不重复已有321项回归；git diff --check通过。

## 未完成与Core review需求

轴线之外的势/力、非均匀密度、场体积平均、moment/opening、树误差、
AMR/Poisson/force/restart与CPU/CUDA生产验收均未覆盖。
Core仍需明确有限cell的密度/点值或平均值语义、近场路线、
远场阶数/opening、独立oracle和分项预算。
工具状态independent-reference-candidate / physics-review-pending。
RZ当前生产二维cylindrical仍为旧极平面，不能发布为RZ能力。
