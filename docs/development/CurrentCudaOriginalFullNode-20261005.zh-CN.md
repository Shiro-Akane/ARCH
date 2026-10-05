# 原 CPU-qualified gravity 完整子组的 CUDA 验证

## 身份与执行

唯一工作区 /home/arch/projects/ARCH-compute-optim，运行HEAD
38f74d5b80ba110873fb1da235b5dadf645cdb68，运行前后clean。
完整ARCH ELF 0203e0b5f097b465898fe68ad62dc734e2cecd680b3e50782682e70fffebe3fc，
复用源2a984091的已有Release/sm89 build，没有重新编译。
590 tracked源码/构建输入聚合SHA和ELF与上一short receipt相同，
报告提交不冒充新binary来源。RTX4070Ti，WSL driver617.14。
原CPU完整子组receipt分别引用JeansExistingFullCpuGates和BoxRadialExistingCpuGates。

既有 Campaign.full()、BoxCampaign.run_checks(quick=False)、
RadialCampaign.run_checks(quick=False)，显式CUDA，原科学参考/输入/阈值未改。
观察包装只调用原入口、保存身份和核对trace，不引入新的数值工作流。

## 结果

74条记录PASS：Wave32、Box14、Radial28。
58真实科学进程，12预期拒绝，另3项时间阶汇总和1项restart身份。
58份trace都device=1且kernels>0；最大residual/原target=.7390315457962875。
原repair事件均0、密度/有效状态及generation等由各原入口检查。
Box另验证有限Host上传，不将CUDA ELF自身当Device执行证据。

| 子组 | 本次指标 | 原门槛／范围 |
|---|---|---|
| Wave32/64/128 | 密度RMS .006568507/.001595788/.000465160 | 64格<=.02，实际t=.1 |
| Euler/RK2/RK3时间阶 | 1.02327/1.01187；2.00386/2.001995；3.00437/3.00216 | 原>=.9/1.8/2.7，参考再减半差1.032728e-5 |
| Wave64标准能量 | Euler .004874674、RK2 8.870214e-8、RK3 6.641701e-6 | 原<=.01 |
| dynamic AMR64/128 | 最大净自力8.956082e-8/6.789718e-9，真实refine/coarsen/no-change | 原<=2e-3且改善，实际t=.4 |
| Wave restart | 14 dataset逐位一致 | 同后端连续/真实续算，不是跨后端参考 |
| 三维Gaussian | 势阶2.041186/2.012819；力阶2.010890/1.999534 | 原>=1.8，独立解析势/力 |
| coupled时间阶 | 2.017101/2.070391 | 原>=1.8；参考差4.332513e-10 |
| 球/柱动态AMR | 能量漂移1.207946e-5/2.573120e-5 | 原<5e-5，真实restart逐位一致 |
| 圆对称静态/low density/BC | 原Gauss、hydrostatic和八个明确拒绝通过 | 1D球/柱不等同RZ二维能力 |

粗32格PCM时间阶探针保留原energy_budget=None，Euler CFL .8约10.3%能量漂移
不能宣布能量合格；64格标准原1%预算保持。Wave密度空间阶不能替代椭圆势/力阶。
原native2D/3D两步、径向dynamic12步及低G等效40步是短输入，非长轨迹；
处理后摘要逐run给stoppingInput和真实最后Plot时间，不从exit0推断达到tmax。
CPU/GPU吻合仅是诊断，不替代独立analytic/Gauss/transport参考。

## 数据与余项

[处理后summary](../../validation/gravity/results/cuda-original-full-20261005/summary.json)
含逐输入与原脚本SHA、聚合源码身份、真实端点、原科学指标和Device标量。
观察包装、完整trace/log、原始H5/plt/checkpoint及ELF留本机
studio/.local/integration/cuda-original-full-20261005，不上传原始场数组。

保护器177.382秒，peak owned RSS3570184KiB，swap增长0；只是单次观察，
不是冻结benchmark、最佳线程/设备峰值或跨平台速度结论。
未修改Core、配置接口或Studio；无前端变化不重复UI回归。

公开CUDA JENS gate仍未改，uniform-lifecycle-1冻结9演化+9restart仍待明确本地验证授权；
本次full是原非均匀Wave/Box/Radial campaign，不是JENS全生命周期或完整O7签收。
RZ生产/科学gate、有限环体runtime force/work、连续参考、axis/viscosity和完整A→B→C→D未关闭。
没有新RZ长跑/benchmark、main merge、tag变更、Windows适配。
