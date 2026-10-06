# 当前 CPU GravityBox／径向原验收子组

按联合计划保留原独立物理参考和预算；本次在同一工作区、
production binary 7d3bd3d 上运行已有 BoxCampaign.run_checks(quick=False)
及 RadialCampaign.run_checks(quick=False)。没有修改源、输入、阈值或预算。
与此前 e9d5cf22 历史 receipt 区分，补当前binary的完整子组证据。

## 结果

42条原有记录全部PASS：Box14、Radial28，含34个真实科学执行、
8个明确拒绝。不冒充完整run_self_gravity主入口或完整O7签收；
此前非均匀Wave32记录与冻结JENS9+9分别引用原receipt。

| 子组 | 当前独立指标 | 原范围 |
|---|---|---|
| 三维孤立Gaussian云16/32/64 | 势阶2.0412/2.0128；力阶2.0109/1.9995 | 原>=1.8，不等同有限RZ环体参考 |
| 扩域同core | 势误差.00915359→.000654946 | 原同dx扩大域后改善，未修改截断预算 |
| 真实mixed云 | 233472 cells，势RMS .00142745、力 .00444441 | 实际粗细界面，原指标通过 |
| 线性输运独立参考 | 相对误差.000261426 | 原共享EOS/物理预算，未调参数 |
| gravity-burn / burn-diffusion | energy-minus-nuclear约8.89e-13/8.92e-13；transport effect8.22e-7 | 核热与流体/势能账本分离，真实输运影响可见 |
| 耦合时间阶 | 2.01710/2.07039 | 原>=1.8；更细参考差4.332572e-10 |
| 球/柱一维静态16/32/64 | 最大Gauss约1.2571e-13/4.0111e-14 | 原独立enclosed-mass，不推导二维RZ支持 |
| 球/柱动态AMR | 能量漂移1.207946e-5/2.573120e-5 | 原<5e-5，质量<1e-12，真实restart逐位一致 |
| 球/柱hydrostatic | 相对声速寄生速度 .00420968→.000847959 / .00547808→.00119641 | 实际t=.02，原<.01且细化改善 |
| 近真空/拒绝 | 正密度、Gauss；八个invalid topology/BC/domain拒绝 | 原支持域，不silent fallback |

所有34个科学执行重新读取state_repairs和gravity trace：
repair events=0，所有原residual<=target。
源/脚本与binary前后SHA一致；运行前clean
HEAD 1a810aeaafa900ecc2ef249a88edaee280d24e08。

## 终点与性能边界

原dynamic径向样本max_steps=12，球/柱实际终点
.02334376606834887/.02257856249397501，未到tmax=.04。
refine/coarsen40步也未到tmax=.1，沿上一报告短范围。
static云/径向只覆盖t=0；不同子例不能混成统一物理终点计时。
hydrostatic三档均实际到.02，coupled原终点由各自输入/输出记录。
summary逐run保存stoppingInput与实际最后output time，不能从退出0推断完成tmax。

本机保护器观测peak owned RSS3857160KiB，swap增长0，未触发；
工具自带elapsed/driver timings保留观测身份，不作为冻结benchmark、最佳线程
或跨平台速度结论。仅CPU单线程原回归，无CUDA执行、正式长跑或Windows工作。

## 复现与数据

使用已有NumPy/h5py venv，运行既有gravity_box.BoxCampaign和
radial_1d.RadialCampaign的run_checks(quick=False)，output必须新本地目录。
观察包装与完整原始H5/checkpoint/trace/log在
studio/.local/integration/box-radial-existing-cpu-20261005。
处理后 [summary](../../validation/gravity/results/box-radial-existing-cpu-20261005/summary.json)
只含指标、身份和诊断；原始数组不上Git。

binary完整SHA：
7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510。
不改既有Studio managed Manifest；完整外部依赖freshness仍unknown。
这推进原CPU验证出口，但不关闭新的RZ连续势/力、axis/viscosity、
完整消费者、CUDA、正式benchmark及批准长轨迹的剩余门槛。
