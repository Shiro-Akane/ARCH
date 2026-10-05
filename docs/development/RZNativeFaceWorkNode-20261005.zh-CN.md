# RZ 原生 face acceleration / potential work 消费节点

基线9481b14aeec102c57d22640901d646497db6aff4，唯一Linux/WSL工作区。
本节点实际改原数学所有者，不解除SelfGravity bind、RZ runtime/regrid或CUDA JENS gate。

## 原所有者接线

GravityWorkspace原面组装提取为gravity_face_rows，构造器直接消费同一结果。
原area/V、curved fragment normalization、signed 2*A/V及native patch owner顺序保留。
只把同一构造暴露为内部可测producer，没有另建solver/物理/接口默认表，
也没有绕过bind gate构造可发布的RZ场。

## 已定位并修复 RZ-WORK-TANGENTIAL-01

既有root/mixed真实tree fixture中，Phi=z应只有轴向梯度。
old两点normal距离插值在coarse/fine的切向位移下不等于fragment中心Phi：
axis mixed的cell527、radial high side，实际work=-.67724867724867721108，
独立native face参考=0；原64-epsilon算术检查失败，失败stdout复制保存在本机。
这不是已运行生产RZ的错误场，bind gate一直阻止该路径。

CompositePoisson::fit_curved_face_value在明确RZ且左右level不同时，
沿原derivative neighborhood、原DenseLUSolver做最小加权校正，满足constant/r/z
在同一fragment center的再现。双方继续消费唯一Phi_face，原功方程不变。
Degenerate明确失败；没有新softening、floor或科学容差。
Existing和RZ同层原面值插值保持原路径；没有修改Poisson derivative/operator数学。
当前关闭限于记录的affine面值偏置，不宣称非线性Phi/force收敛或旋转能量验收。

## 真实检查与结果

原axis/off-axis、2roots→5mixed，3584cells／14336active side rows：
Phi=z独立解析g=(0,-1,0)，原生有符号有限体积功逐face独立求和；
原CSR消费者检查area/volume/interpolation/sign。
实际原Host GravityExecution运行CellAcceleration，验证inactive phi=0、
轴向g=-1、inverse timescale=1/dz。
64 epsilon仅是该工程组装的scale-weighted FP64 arithmetic检查，
没有替代或放宽任何连续物理、16-epsilon JENS、residual或>=1.8科学阈值。

CPU原4项CTest PASS：composite_poisson_analytic、self_gravity_lifecycle、
composite_poisson_contract、self_gravity_physics。
其中physics脚本仍使用未变CPU ARCH，不能当作新source生产binary证明；
新数学/lifecycle target实际链接当前archive。
当前archive进入实际Cartesian Runtime重编6源，4→8→4、7gather/time0/steps0 PASS。
CUDA-enabled同一Host CTest 1/1 PASS，不是Device RZ execution。

初轮失败仅加具体row诊断，保持原断言；修复后全部通过，failure未隐藏。
原架构审计和diff检查通过。只重建受影响测试/library，不重新configure。
生产CPU/CUDA ELF未改变，复用准确匹配旧receipt，不重复旧完整科学包；
新的源码freshness不能用旧生产binary/Studio Manifest冒充。

## 证据与剩余

[处理后summary](../../validation/gravity/results/rz-native-face-rows-20261005/summary.json)
记录source/archive/test ELF、初轮标量finding与实际Runtime证据。
原始log/诊断/ELF保留studio/.local/integration/rz-native-face-rows-20261005、
同prefix build logs与rz-native-face-runtime-20261005，不上传原始数组。

RZ-BOUNDARY-DISPATCH-01 OPEN：还需要typed current full-ring边界/RHS/residual、
实际全块stage/source identity/epoch失效与force-work/publication。
真实ring源与continuous Phi/force、原空间阶、axis/viscosity/旋转收支及A→B→C→D
完整科学出口均保持。无新RZ演化、H5、checkpoint、长跑、benchmark或Windows工作。
