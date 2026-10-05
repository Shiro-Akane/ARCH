# 当前 CPU Release 科学复验 checkpoint

## 身份与必要性

最近环体/Runtime源码改动后，旧生产ELF仍为5d454c03...。
本次在唯一源码工作区现有build-cpu增量构建，CMAKE_HOME_DIRECTORY实际为
/home/arch/projects/ARCH-compute-optim，Release，target ARCH，parallel28。
不configure，不改变科学能力、输入、物理常数或阈值。

新CPU ELF SHA-256：
32f7b13972ac9381076b06e04c581900065a246796e911bdd161479d86f31b8e。
编译源码HEAD f20a1eefad62eccc015752eb0da68423502d00d6。
当时唯一未跟踪文件为连续面力参考脚本，不影响Core构建。
该checkpoint只确认CPU executable与列明科学receipt；不自动改写Studio Build Manifest。

## 冻结 JENS

复用原uniform-lifecycle-1 runner，输入和判断原样；
只更换新输出目录。CPU、threads1，维度1/2/3，各disabled/output-only/active。
九组真正演化至.02，真实.01 checkpoint分别续算至.02，九个restart均逐位一致。
active cells128/4096/131072；uniform fields严格原值，Phi/力/速度0，
质量及能量漂移全部0。disabled与output-only原生state/solve-count逐位一致。
accepted-state transaction覆盖和JENS参考检查沿用原consolidate。

首次manifest使用错误路径self/GravityBoundary.cpp，未写成；
随后核实真实路径gravity/GravityBoundary.cpp、runner bytes与ELF不变并补齐记录。
同一科学进程持续运行，未重启，不以错误身份记录覆盖结果。

## 原批准科学矩阵

同一ELF完整运行既有run_self_gravity.py主入口，共80条验收记录PASS：
原Campaign.full、BoxCampaign、RadialCampaign子组及主入口六个配置拒绝。
与先前74条子组receipt的区别是主入口额外六个拒绝，不是新增物理场景。
Euler时间阶1.02327/1.01187，RK2 2.00386/2.001995，RK3 3.00437/3.00216。
原质量/标准能量/势力/残差/空间阶/真实restart和非法配置检查没有放宽。
原energy_budget=None的粗PCM时间探针仍只签收时间阶，不能扩大为能量合格。
一维cylindrical径向通过不等于二维RZ科学签收。

## 复现与处理后证据

CPU增量命令：cmake --build build-cpu --target ARCH --parallel 28。
完整原矩阵：使用既有NumPy/h5py Python运行
validation/gravity/run_self_gravity.py --arch build-cpu/bin/ARCH --output NEW_LOCAL_DIRECTORY。
冻结JENS：调用BoxCampaign(...,backend='cpu',threads=1,measure_resources=True).
jeans_uniform_lifecycle()，以check_jeans_uniform.consolidate按原九组合并；runner hash随identity记录。

汇总：validation/gravity/results/current-cpu-release-20261005/summary.json。
原始数据在studio/.local/integration/current-cpu-release-jens-20261005/、
studio/.local/integration/current-cpu-gravity-gates-20261005/；build/guard全日志本机。
没有提交H5/plt/checkpoint/ELF/原数组。

构建22.219秒、JENS44.091秒、原矩阵35.080秒，swap0，guard均未触发。
同机另有一个独立Decimal面力参考进程；这些用时不作为冻结CPU/CUDA benchmark。
CUDA public JENS审批、新RZ科学/axis/viscosity、完整consumer与长跑门槛仍保持。
