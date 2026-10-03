# 径向 full CPU 正向增量与 gravity gate 清单

## 本轮6个样本

原RadialCampaign非quick相对quick新增的球/柱static16、static32及hydrostatic16全部PASS。
只执行此前未完成分辨率；static64/hydrostatic32/64直接复用已有处理后证据，
复用前明确比较binary SHA一致。原参考、定义、预算、输入控制不变。

主CPU executable仍来自clean b82205e2、SHA
7d0360de4ac9a3429d2a8ffec3908ca4424f2c7716096f2c44ad189662a9d8c4；
源码d80780ec含未编入主ELF的诊断增量；不把当前HEAD冒充binary来源。

球/柱16、32的势/力/Gauss最大相对误差均<2.7e-14，原要求<1e-7。
静水原t=.02，在16/32/64上的寄生速度/中心声速：
- spherical .00420968→.00211311→.000847959；
- cylindrical .00547808→.00283521→.00119641。
满足原<.01及严格随细化下降要求，不宣称长期well-balanced精确保持。
Poisson residual、完整domain、有限值、零floor均按原runner检查。

本轮耗时约.272秒，不用于正式性能验收。输入hash、原始H5/checkpoint
相对文件名/size/SHA和保留目录在同名Summary.json；原数组、日志及编排脚本留本机。
无生产修改、重build、CUDA、Windows或push/tag。

## 当前原 gravity campaign 覆盖清单

| 原入口 | 真实证据 | 当前状态 |
| --- | --- | --- |
| Jeans spatial/time/energy/dynamic-AMR/native2D、mixed2D、uniform3D | JeansFullCpuFinding-20261004 | 原预算已通过 |
| Jeans native-mixed-3D | 原CLI FAIL；PoissonCoarseDiagnostic真实constructor证据 | FAIL，coarse ratio4>2待Core策略 |
| Jeans restart与身份拒绝 | JeansFullCpuFinding独立Restart7条，14numeric raw bytes | 通过；不清除full FAIL |
| Box quick cloud1/thermal/compact burn paired | CurrentFullCpuRegression、GravityQuickInputMigration | 已通过 |
| Box full cloud convergence/domain expansion/mixed与coupled-time | BoxFullCpuAdditions-20261004 | 原新增范围已通过 |
| Radial static16/32/64、dynamic/restart、hydrostatic16/32/64、near-vacuum | RadialInputMigration及本报告 | 原positive范围已通过 |
| Radial两种regrid_cycle | 仍保留原historical gravity_G=1e-20 | 未执行，等效换算待Core |
| 全14个gravity negative contracts | GravityNegativeContracts-20261004，actual INVALID_RANGE | 通过；无重复/缺项掩盖或科学输出 |

此表是已完成分项的覆盖索引，不是把独立partial结果合成为full green。
run_self_gravity.py的原整套入口保持完整：3D mixed failure与historicalG项没有删掉/skip。
原full CTest69/71失败JUnit仍保留；ui_expansion scoped修复通过另行记录，
没有生成拼接71/71或把CPU阶段关闭。JENS/RZ科学方案与整体平台出口另有待审事项。

本轮进一步缩小CPU gravity尚需Core决定的具体项目：
1. 内部Composite coarse mesh的spacing ratio约束/策略，见真实失败及上下文；
2. historical非物理G=1e-20的等效场景换算与对应保留预算。
不得因此提前统一CUDA或用工程fixture宣称科学验收。
