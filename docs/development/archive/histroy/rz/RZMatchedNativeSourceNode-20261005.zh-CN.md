# Actual static native source -> independent continuous boundary Phi diagnostics

基线 d4f9cfcf040ad7f400dd8ca87cc6849bb2b69c2e；
唯一工作区 /home/arch/projects/ARCH-compute-optim。
状态：实际源记录接线完成；不是完整连续Phi/force科学签收。

## 真实数据接线

native_ring_solved_probe 新增导出：
- 直接用于 tree.update 的原 density，每个实际叶的 level/index/center±half-width bounds；
- ring.source 当前完整 topology/time/G/revisions/dependencies 和 source_generation；
- op.face_gradient 从实际 solved potential 与 ring boundary values 求出的最终梯度。

导出前 require_current_ring，未从 Poisson RHS 反推 density。
同一既有 mixed 求解再运行以取得新字段：2个root origin0/0.5，
每例28叶、原 rtol=1e-10/atol=0、face target1e-18、caps和max_cycles完全不改。
新增字段由真实对象取值，不改矩、核、求解器、scientific Core或能力gate。
身份仍是静态 numerical-domain abstract dependency；
不能冒充Runtime所有block的stage/regrid source publication。

rz_matched_native_reference.py 检查完整输入stamp、精确Fraction root/实际leaf bounds，
从显式density组装独立full-ring源并hash身份/源几何/密度。
使用同一实际boundary face.center；G将Decimal数学参考显式线性缩放到
输入stamp中的精确FP64常数后再比较，不混淆常数表示误差。

## 修正一次无效覆盖

初次axis-only筛选实际返回0点：native operator不发布零面积轴面。
该 axis-summary 留本机，不作为通过证据。
最终改用真实全部外边界点：axis-origin例12点、off-axis例16点，共28点，
每点记录其contact/exterior源叶数。接触势用已有批准的Duffy诊断，
其余源叶用已有源外K/E点势，逐叶真实密度相加。

单独新增 potential_reference，不改变原参考工具对inside/contact力的拒绝：
只有点Phi，certified=False；不能把contact Phi输出当成力oracle。
新增实际gradient记录保留供后续force比较，本节点不宣称力已验证。

## 检查与结果使用

25项相关工具测试PASS，包括原工具回归、原密度不随RHS替换、
source identity/density变化hash失效、错stamp/rounded bounds拒绝、
contact potential与force严格分离、失败保留与禁止覆盖。
新C++字段版本的真实mixed solve及独立Fraction原离散请求：2例56叶PASS。
架构审计、diff check PASS。仅构建既有CPU Release arch_composite_poisson，
parallel28、memory guard启用；无configure、新build tree或simulation。

边界势分别改变order16/32；另在一个真实contact observer分别改变t partition和
Decimal precision。精度/阶数/分区差均只作估计，不形成可靠误差界或新科学阈值。
最终scalar结果、输入/源码/probe SHA见：
validation/gravity/results/rz-matched-native-source-20261005/summary.json。
两例最大production差：
- order16约5.68006e-14 / 5.67920e-14 cm^2/s^2；
- order32约3.76047e-15 / 3.76032e-15 cm^2/s^2。
order16→32变化约5.30402e-14 / 5.30317e-14。
选中真实contact点的t partition1→2变化2.82002e-15；
precision80→100变化1.38411e-86。
这表明当前差受求积分区／阶数影响，不能因为Decimal精度稳定就称参考收敛。
此诊断不足以核验production所请求1e-18点势可靠界；
不得用两者不符直接认定production不准确，也不得静默提高门槛。

诊断差异不能直接归因于production错误；未经可靠reference error预算，
不能从本次观测倒推足够精度或关闭科学finding。

production CPU ARCH ELF未改，沿用已匹配冻结JENS9+9，不重复执行。
原始phi/rho/rhs/face数组、完整stdout/stderr、初次失败工具日志、
build和reference记录均留本机：
studio/.local/integration/rz-matched-native-source-20261005。

## 剩余验收

真实Runtime all-block dependency/stage/regrid publication；
连续cell Phi、face force、side/cell gather与原空间收敛；
源内／接触force参考和分项budget；
RZ-AXIS-01/RZ-VISC-01与完整角动量消费者。
本节点不关闭上述gate，不启用完整production RZ/CUDA或新RZ长跑。

复现：
    build-cpu/arch_composite_poisson native-ring-mixed-probe > <new local record.json>
    python3 validation/gravity/rz_native_ring_solved_reference.py --probe-record <record.json> --output <new discrete summary.json>
    python3 validation/gravity/rz_matched_native_reference.py --probe-record <record.json> --output <new diagnostic.json> --order 32
    python3 -m unittest discover -s tests/tooling/validation -p 'test_rz_*reference.py' -v
