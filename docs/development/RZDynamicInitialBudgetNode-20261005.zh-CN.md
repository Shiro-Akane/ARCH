# RZ 初始动态预算：原所有者接线节点

基线0ba26b5a3222c6a085272a50fede0cbf4d139242，唯一Linux/WSL工作区。
动态初始份额和四例真实native discrete原请求PASS；
不是完整Runtime、连续势/力、RZ release或Device ring签收。

## 实现与语义

CompositePoisson::native_rz_boundary_sensitivity消费原native face enclosure、
同一ideal B及native RMS，返回K_upper=norm(abs(B)*1)上界，单位cm^-2。
这是operator geometry sensitivity，没有伪造unit-error source/observer证书。

GravityBoundary::propose_ring_budget要求同一current tree/source/topology/generation。
原computed isolated source与真实density进入原source construction bound，
computed source native norm lower扣其native source-error norm upper，
得到保守source scale L；然后向下舍入：
T_initial=max(atol,rtol*L)，delta_initial=(T_initial/2)/K_upper。
原G、rtol/atol、工作上限没有改变，未插入floor。
返回状态是Proposed/ZeroBudget/InvalidInput/UncertifiedInput/Overflow，
不是Accepted、Converged或field stamp；状态带完整source与generation。

**source scale不是最终RHS下界，T_initial不是最终T_safe。**
本提议只是初始工作分配；实际boundary值改变后，source/B construction/
potential/assembly/A/evaluation须按同一原请求重新组合。
只有最终完整assessment可以证明native residual满足；不能以提议或solver report
代替这个检查，更不能以native residual通过宣布连续势/力签收。
生产SelfGravity尚未调用本接口，公开bind/config/API门槛保留。

## 真实求解和独立证据

原native_ring_solved_probe新增dynamic模式，原固定模式保留。
实际rho=1、共享G、axis/offaxis uniform/mixed四例共88cells，
CPU原execution产生source→当前tree→finite-ring→native RHS→原solver→
apply/residual→assess_native_ring_rhs。rtol=1e-10、atol=0、cycles200，
65536 boxes/leaf与100000 total work上限未变。

实际dynamic face targets约8.53659e-19–8.84246e-19，
由各operator/source计算，未沿用固定1e-18。
四例原total/T_safe最大0.33271654；
uniform 34.224秒、mixed 40.580秒，工作计数252/336/444/592。
这是本次数学检查费用，不是CPU/CUDA benchmark。

原独立Fraction solved工具分别验证uniform32cells、mixed56cells，
ideal RHS box、actual evaluation、完整原请求全部PASS。
离线budget工具独立root geometry/Gram核对每个cell sensitivity上界、
exact K²、真实source box的norm lower、向下T及half allocation，四例PASS。
Decimal sqrt仅诊断，实际检查使用Fraction平方和，不拿它充当证书。

原self lifecycle新增zero、1e-6/1/1e6 source scale、invalid rtol、stale request，
验证无floor、current身份、单调scale及合法half份额。
CPU lifecycle13.32s、原composite analytic3.25s、CUDA-enabled Host lifecycle13.12s PASS。
CUDA-enabled Host不是Device ring算法；不开放CUDA JENS或RZ。
actual Cartesian Runtime重编六源/链接当前archive，4→8→4、7gather、
time0/steps0回归PASS；ELF与前节点相同，原现有路径未消费新增接口。
architecture/diff PASS。

受影响CPU库/两个test增量6.034s、owned RSS峰2256104KiB；
后续probe源增量4.026s、386336KiB；
CUDA-enabled Host增量5.021s、774092KiB；swap均0。
未configure或重编生产ARCH ELF；旧生产receipt保留原身份，不能声称current source fresh。

## 交付与后续

[处理后摘要](../../validation/gravity/results/rz-dynamic-budget-20261005/summary.json)
记录source/test/生产ELF、当前四例与独立工具结果。raw数组、日志和ELF保留
studio/.local/integration/rz-dynamic-budget-20261005及同prefix build logs，
actualRuntime在rz-dynamic-budget-runtime-20261005，不上传H5/plt/checkpoint。

下一步仍是原Workspace/SelfGravity的全块密度、source generation、
失败/重绑/regrid失效、动态份额和完整native账本，再接force/work及资格发布。
本节点不替代该完整Runtime，也不关闭一般坐标、continuous Phi/force、
axis/viscosity/angular A→B→C→D或CPU→CUDA科学出口。
没有运行simulation、新长跑/计时或Windows适配；公开门槛保持。
