# 当前 CPU 非均匀 JeansWave 原验收子组

依据 O7JeansRzReviewQuestions 第4.1/6节：保留原非均匀 JeansWave 的
质量、能量、势力、残差和时空检查；不以均匀 JENS 生命周期代替。
本次完整运行既有 Campaign.full()，没有修改科学输入、独立参考或阈值。
不执行 run_self_gravity 主入口中的外围 GravityBox/径向矩阵；
冻结 JENS 9+9 和上一节点三个获准短样本不作为新物理 gate 重定义。

## 结果与原门槛

32条记录 PASS：24个真实演化进程、4个明确拒绝进程、
3项时间阶汇总、1项checkpoint逐位身份。
运行前 clean HEAD cfa0c7ed3b6c50fbec55efff16f85b6c0c126307。
binary仍7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510，
运行前后 binary 和列明输入 fingerprints一致。

| 子组 | 当前结果 | 真实覆盖 |
|---|---|---|
| 32/64/128格空间波形 | t=.1，扰动RMS .006568507/.001595788/.000465160 | 原64格<=.02通过；密度阶诊断2.0413/1.7785，不等同P3势/面力阶数验收 |
| Euler 时间阶 | 1.02327/1.01187 | 原>=.9 |
| RK2 时间阶 | 2.00386/2.001995 | 原>=1.8 |
| RK3 时间阶 | 3.00437/3.00216 | 原>=2.7 |
| 独立细步参考再减半 | 密度向量差1.032728e-5 | 小于各被比较时间误差1%，原条件通过 |
| 64格标准能量 | Euler .004874674，RK2 8.870214e-8，RK3 6.641701e-6 | 均<=.01，分母为初始引力势能幅度 |
| dynamic-amr-64 | t=.4；refine6/coarsen4/unchanged248；2054 solves | 原混合层级、lease generation唯一、residual/target通过 |
| dynamic-amr-128 | t=.4；refine23/coarsen20/unchanged471；4102 solves | 同上 |
| 两档AMR最大净自力 | 8.956082e-8/6.789718e-9 | <=2e-3且随细化下降 |
| 连续/真实续算 | t=.1，末态14 dataset逐位一致 | 当前CPU通道checkpoint身份；不代表跨后端 |
| 四个拒绝 | retired G、rtol/atol/max_cycles身份不匹配 | 错误清楚，未发布H5或partial |
| native 2D/3D与mixed | 原两步Driver短样本通过 | 实际时间均小于.02；不是长时多维科学结论 |

所有被检查演化均repair events=0、密度有限、原residual通过、
generation不复用、质量漂移<=1e-12。
波形误差与空间阶保持原 P3P4CompositeGravity 解释：
有限振幅的密度收敛不是椭圆算子phi/face-force >=1.8的替代检查。

**能量边界：** 原32格PCM时间阶探针使用 energy_budget=None。
Euler CFL=.8、t=.3 的势能归一化能量漂移 .1033803；
其他粗步也分别列入摘要。时间阶通过不能把这些探针称为能量合格轨迹。
原1%能量预算仍只用于列明64格标准，不因结果通过而扩大适用域。

## 复现与数据

使用现有 NumPy/h5py 环境，从新本机持久目录运行现有
validation/gravity/run_self_gravity.py 中 Campaign(executable,output).full()。
本次观察包装只打印进度和保存已完成记录，不修改数值执行；
包装SHA、确切输入/ELF身份及标量结果见
[summary](../../validation/gravity/results/jeans-existing-full-cpu-20261005/summary.json)。

原始H5/checkpoint、全量日志/trace留在
studio/.local/integration/jeans-existing-full-cpu-20261005。
仅处理后记录上Git，不上传原生数组或原始文件。
保护器观测 owned RSS峰值461848KiB、swap增长0、未触发；
不是正式性能计时或硬件峰值认证。

完整O7仍未完成：当前JENS覆盖范围、RZ连续Phi/force与axis/viscosity、
全消费者签收、统一CUDA、冻结benchmark及批准长跑继续按计划。
未启动CUDA、不更改能力门槛、不开展Windows适配。
