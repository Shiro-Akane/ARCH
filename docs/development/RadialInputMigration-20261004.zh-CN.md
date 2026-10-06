# 径向 CPU quick 输入迁移与真实 Restart

## 输入 finding 与最小修改

配置 v3 的 GravityBox.par 为周期一维 sample 显式记录 center_x=5e7。
radial_1d.py 的 config 原先继承该 base 而不覆盖 center_x，导致径向 Gaussian
中心随输入迁移而改变。原 8fc0dd25eefd2243e8c36f85440bac46994e2e73
GravityBox.par 没有 center_x，GravityBox.cpp Setup 的 radial 默认明确为0。

因此径向生成器显式 center_x=0，恢复原已验收初值；最后 changes 合并仍保留
调用方覆盖优先级。不改生产 Core、独立 Gauss/静态/能量参考或阈值。
本次无需重新编译，主CPU ELF SHA为
7d0360de4ac9a3429d2a8ffec3908ca4424f2c7716096f2c44ad189662a9d8c4，
构建 source clean b82205e2c6414f2d8783063d6ff086c06f66404c。

## 真实覆盖与结果

先 capture 原方法输入，8份 inspect-config 均exit0；再实际执行以下12个已有样本：
球/柱各 static64、dynamic-amr、restart、hydrostatic32/64、near-vacuum。
dynamic使用原 .04/12，hydrostatic原 .02，near-vacuum原 .01/2；
没有新增终点或改变独立预算。

| 指标 | spherical | cylindrical | 原预算 |
| --- | --- | --- | --- |
| 静态势相对误差 | 3.5102e-14 | 4.2353e-14 | <1e-7 |
| 静态力相对误差 | 1.2581e-13 | 4.0182e-14 | <1e-7 |
| 动态 Gauss 最大相对误差 | 1.2131e-8 | 1.0828e-11 | <1e-7 |
| 动态质量相对漂移 | 6.5871e-16 | 6.8180e-16 | <1e-12 |
| 动态能量相对漂移 | 1.20795e-5 | 2.57312e-5 | <5e-5 |
| 静水32→64 寄生速度／中心声速 | .002113→.000848 | .002835→.001196 | <.01 且细化下降 |

两种动态 AMR 的 refine/no-change lifecycle、初始混合层、原点完整domain、
gravity residual与零floor repair检查通过；near-vacuum正密度约1.000000918e-12。
原 np.array_equal restart检查之外，再只读核对所有20个numeric dataset的raw bytes，
包含signed zero一致；每种连续/续算最终checkpoint整个文件SHA也相同。
21个dataset中的string由原runner检查。精确身份与全部记录在同名Summary.json。

## 保留的 gate

这是已有一维球/柱正向 quick 验证，不是二维RZ或完整self_gravity验收。
regrid_cycle中的historical gravity_G=1e-20完全未改、未执行，
科学等效换算仍待Core；没有把它删除、skip到完整runner或修改原预算。
本轮未重跑负向配置/完整CTest，原69/71失败JUnit原样保留。
完整入口run_self_gravity.py仍包含历史G项，不宣称full gate通过。

validation/gravity/radial_1d.py仍为复现入口：
RadialCampaign.static(geometry,4)、dynamic_and_restart(geometry)、
hydrostatic(geometry,True)、low_density(geometry)，两种geometry均执行。
原始输入、inspect、H5、checkpoint、run logs与局部编排/byte-audit脚本留本机
studio/.local/integration/radial-input-migration-20261004；
提交只包含生成器修改、处理后摘要和本报告。无CUDA、Windows、push/tag。
