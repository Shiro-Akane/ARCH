# CPU gravity negative contracts：配置 v3 迁移

## Finding

原有14个negative CLI检查初次11 PASS /3 FAIL。
其中部分所谓PASS并不覆盖目标：Campaign加载JeansWave.par时未strip键，
gravity_rtol = ... 与override gravity_rtol=0同时写入，实际为DUPLICATE_PARAMETER；
atol同类。多维旧输入另有新契约要求的axis/case缺项，
sphere fluid-face虽命中目标文字仍夹杂MISSING_PARAMETER。完整原诊断保留本机。

## 修正与真实复验

仅修改validation/gravity/run_self_gravity.py与radial_1d.py：
- Campaign加载base时strip key/value，override在dict中真正替换同一键。
- 3D isolated负向样本明确x2/x3 min=0/max=1，来自原GridConfig默认。
- spherical二维负向样本明确x2_min=0/x2_max=1与center_y=0；
  full-turn fluid-face样本保留原2π上界，补原x2_min/center_y。
  原8fc0dd25 GridConfig/GravityBox Setup分别明确这些旧有效值。
- 两类reject新增断言：不能含DUPLICATE_PARAMETER/MISSING_PARAMETER掩盖目标；
  不能产生任何H5（包括checkpoint）或.partial科学临时文件。

14项复验全部PASS，逐日志额外核对全部为INVALID_RANGE且包含原目标诊断，
无重复/缺项、无科学输出。rtol/atol/cycles、Cartesian isolated维数/流体拓扑、
球/柱isolated/内边界/负半径及二维球full azimuth/fluid-face均覆盖。
详细原文、input SHA和generator SHA见同名Summary.json。
没有修改Core验证规则、物理定义、独立参考或误差门槛。

## 边界

本次只执行原negative CLI配置拒绝，不进入simulation timestep。
主CPU binary SHA 7d0360de4ac9a3429d2a8ffec3908ca4424f2c7716096f2c44ad189662a9d8c4，
build source clean b82205e2c6414f2d8783063d6ff086c06f66404c，无重build。
完整self_gravity仍包含historical radial G=1e-20，换算待Core；原full 69/71失败JUnit不改。
不因本轮14/14把CPU/CUDA/O7/O9总体标成通过。

原输入、初次失败、复验日志和编排脚本保留
studio/.local/integration/gravity-negative-contracts-20261004。
提交生成器修正、处理后摘要与报告，无原始H5/checkpoint/ELF，未push/tag。
