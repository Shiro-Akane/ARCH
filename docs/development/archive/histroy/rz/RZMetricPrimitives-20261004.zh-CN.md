# 完整旋转 RZ 度量共享原语

2026-10-04；基线66cc5592638bd3e8cabf42e034342ddca764ae19。
状态：metric primitive implemented / CPU arithmetic engineering-pass；
整条RZ及physics-review仍未完成。

## 单一数学所有者与接入边界

src/grid/GridMetrics.h中新增显式GridMetrics::Rz原语，使用已有ARCH_HOST_DEVICE和共享two_pi：
CellVolume(rL,rR,dz)=2pi·cylindrical_annulus_volume(rL,rR)·dz；
RadialFaceArea(rFace,dz)=2pi rFace dz；
AxialFaceArea(rL,rR)=2pi·cylindrical_annulus_volume(rL,rR)；
PhysicalSpacing(dir,dr,dz)按active r/z方向给出dr/dz，不使用r·dphi。

复用已有factored radial integral，不在CPU/CUDA分别实现，也不建第二个几何所有者。
输入前提是非负有序r、正dz以及active direction 0/1；叶函数没有新floor、单位换算或容差。
测试覆盖列出的可表示数值范围，不宣称任意极端输入的中间溢出均已解决。

本次不改变现有GeometryView/Geometry枚举、CellVolume/FaceArea/PhysicalSpacing dispatch。
现有二维cylindrical仍为polar；RZ源项、AMR、椭圆/引力、身份和API尚未同步，
因此不将仅度量实现开放为生产RZ，不给旧极平面输入或checkpoint自动改义。
新增原语是后续整条迁移使用的共同数学入口，不是第二套RZ求解器。

## 独立参考与有意义的检查

tests/math/geometry/RzMetricCases.h保存10组70/100位Decimal定义积分参考：
轴线、普通环体、极薄环体、大/小长度及不同轴向尺度。
输入取精确binary64端点，使用高精度pi与展开平方积分；
不调用生产cylindrical_annulus_volume或Rz函数产生期望。
validation/amr/rz_metric_reference.py只读核对fixture，两种精度舍入一致。

现有arch_curvilinear_metrics target消费共享fixture，
在原有2e-12 arithmetic gate下RZ最大相对误差1.64562e-16。
该门槛沿用既有度量算术检查，不是新定的RZ动力学/引力科学阈值；
tiny cell使用相对误差，axis zero face必须严格正零。
此外验证原生长度不被半径缩放，线性矢量场div(r e_r+z e_z)=3、
面积差/体积对应平均1/r、2×2源体体积分区和粗细轴向面面积相加。
这些是度量代数/积分证据，不冒充实际AMR迁移/reflux或Euler演化。

现有Cartesian/cylindrical/spherical各维度的CFL、常压源项平衡及黏性空间/轴线检查
同一target通过；existing metric最大相对误差3.03178e-16。
这里核对的是旧数学未被当前新增原语改变，不是新RZ源项已通过。

## 构建、复现与准确身份

仅使用当前source root绑定的既有build-cpu Release树构建arch_curvilinear_metrics，
单个测试TU及link；没有独立configure、重编ARCH或simulation。
首次编译发现新增测试输出使用多字符换行literal，已修正后定向重建/复测。
最终build无warning，CTest curvilinear_metrics 1/1通过。

~~~bash
python3 validation/amr/rz_metric_reference.py
cmake --build build-cpu --target arch_curvilinear_metrics -j4
ctest --test-dir build-cpu -R '^curvilinear_metrics$' --output-on-failure
~~~

准确source输入hash、测试ELF SHA、构建身份、oracle标量见Summary。
编译输入是基线加明确changedInputs，不把本报告提交HEAD冒充已编译身份。
原始build/test日志和测试ELF留本机；提交只有源码、fixture、工具和处理摘要。
本轮src仅GridMetrics.h新增原语；未改科学定义、验收阈值、原始数据或root STATUS。
没有CUDA验收、push/tag/main merge。

## 后续依赖

先完成O7.1所需的一般EOS、诊断/AMR条件、候选父态规则与参考预算确认。
RZ随后需要GeometryView/物理坐标、r/z/phi几何源项、seam、输运、
AMR保守迁移/flux、椭圆算子和有限环体边界、geometry/checkpoint身份/API/Studio整条接线。
O7.4近场/接触/源内路线、近远切换/阶数与分项预算仍等Core科学决定；
既有O7_RZ_IMPLEMENTATION_MAP和独立axis/offaxis参考是review材料，不能替代批准。
CPU整条与科学出口通过后才统一处理CUDA及冻结物理终点的性能/长时轨迹。
