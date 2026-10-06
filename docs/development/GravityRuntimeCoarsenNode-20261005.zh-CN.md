# Gravity Runtime refine / coarsen / no-change：合法 AMR 阈值 fixture

基线2cca2b42b43e003aac98f9765357f2d75e600515。
统一计划第7项的共享生产身份补证；唯一ARCH-compute-optim工作区。
本节点修正工程fixture并完成同一实际Runtime生命周期，
不修改scientific Core、生产配置规范或冻结科学阈值。

## 先前证据限制与修正

前一fixture直接构造SimConfig，将refine_threshold设.001，
但保留默认derefine_threshold=.2；不满足Core
0 <= derefine < refine <= 1。先前执行结果保留，不能作为完整合法输入验收。

本次明确成对给定.001/.0005，并直接调用现有
arch::config::relations::CurvatureThresholds断言。
这是typed工程fixture的AMR关系修正，不是放宽科学门槛；
未宣称fixture通过全部v3公共输入解析或代表一个冻结物理模型。
Core relation源码与production ELF未变。

## 真实生产调用链与新增覆盖

完整重新执行此前全块、非首块version/不可读拒绝、
Scratch/Next actual gather、失败失效、Runtime refine路径；
随后在同一Runtime中继续：
- 保留有效DENS indicator；实际Current density设为常量1并明确发布新version，
  真实halo refresh后perform_regrid。不是禁用所有indicator绕过配置。
- 8个细块coarsen回4个root blocks，epoch2->3；
  新ledger读取退役fine handle明确失败。
- GravityStage再次prepare_current，真实rebind/gather64cells；
  所有block/offset密度逐项相同，周期constant source得到rhs/residual/target精确0。
- 再执行实际no-change transaction；handles/epoch保持，
  下一prepare发行新lease，仍真实收集全domain。

实际序列4->8->4->4 blocks，epoch1->2->3->3。
7次成功gather，generation1/2/5/6/7/8/9；两个失败lease仍退役。
residual<=原target全部通过，没有新增zero floor。
stage请求time是输入identity；Controller始终time0/steps0，
不把这些人工准备当成实际Hydro演化。
Capture仍只委托现有Host execution/solver，未建立第二数学路径。

## 验证与复现

复用既有runner可信compile/link命令，重编fixture及5-TU调用链中的4个生产TU，
链接现有CPU archive；无configure/新build tree。
最终memory guard24.085s、peak owned RSS1159368KiB、swap0、无guard stop；
这不是正式计时样本。
实际fixture、architecture audit、diff check PASS。
两项旧生命周期检查代码未变，复用前节点receipt，不重复执行。
production CPU ELF仍7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510，
继续复用匹配JENS冻结9+9。

处理后身份/diagnostic摘要：
validation/gravity/results/gravity-runtime-coarsen-20261005/summary.json。
raw编译日志、ELF、gravity_solves.tsv留本机：
studio/.local/integration/gravity-runtime-coarsen-20261005。
无H5/plt/checkpoint。

复现：
    python3 validation/gravity/run_gravity_runtime_contract.py --build build-cpu --output-root <new local directory>

## 全项目范围与门槛

StudioIntegrationProgress总表更新了旧JENS“生产尚未接通”描述；
现在已有CPU条件输入、accepted-state/regrid/output/checkpoint、Host与Linux消费者。
已有uniform-lifecycle-1短子gate不是完整JENS或CUDA验收。
RZ已交付可靠环体势/离散账本与实际Init/IO/checkpoint候选，
但continuous Phi/force、axis/viscosity、完整RZ实际消费者科学gate未关闭。

本节点是Cartesian共享身份链，不把它转记RZ physical PASS。
restart、device migration、实际Hydro演化、CUDA/冻结benchmark/批准长时子集
仍按完整目标继续；整体未完成。原始数据继续留本机，无Windows工作。
