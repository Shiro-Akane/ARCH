# 当前 CUDA Release 原科学子组复验

## 身份与范围

当前CPU新ELF冻结JENS9+9及完整原矩阵80记录PASS后，按批准子集统一CUDA。
唯一工作区现有build-cuda，Release，ARCH，parallel28，不独立configure。
CUDA编译HEAD8aff815b109921151b40a8680b6d886541886c22；
与CPU编译HEADf20a1eef之间src/simulation/CMake/cmake无差异，代码树一致。

新CUDA ELF SHA-256：
1cbbd6952f4970f89ef5071b9e3f18d9ec619ec92824f0ad9928455f6e27eb53。
NVIDIA GeForce RTX 4070 Ti，driver617.14，reported memory12282MiB。
这是实际设备，旧5070Ti描述不使用。

## 原批准矩阵结果

原样复用cuda-original-full runner，只更换本机输出目录。
Wave32、Box14、Radial28，共74记录PASS（58真实演化进程、12拒绝、4汇总）。
每个成功run的gravity_solves.tsv均要求所有行device=1、
kernels>0；58条run evidence通过，共4311863 kernels。
最大residual/target=0.7390315457962875，原科学阈值、inputs和scope不改。
没有Host fallback，不以进程exit替代实际Device证据。
binary与所有tracked build inputs在运行前后fingerprints一致。

CPU主入口80记录另外有六个既有参数拒绝；此CUDA74子组与此前已批准
CUDA runner一致，不将记录数差异当作改变物理验收。
粗PCM时间探针energy_budget=None仍不能扩大为能量合格。
一维cylindrical通过不是二维RZ签收，uniform JENS public CUDA gate没有修改或执行。

## 复现、证据与限制

同现有Campaign.full、BoxCampaign.run_checks(False)、RadialCampaign.run_checks(False)，
分别选择明确CUDA backend，逐项保留原判据；现有runner SHA随summary记录。
处理后summary：validation/gravity/results/current-cuda-release-20261005/summary.json。
全部输入fingerprints、原始H5/plt/checkpoint、stdout/full日志保留
studio/.local/integration/current-cuda-original-gates-20261005/。
构建全日志：studio/.local/integration/current-cuda-release-20261005.build.log。

构建71.308秒，campaign180.367秒，swap0，guard未停止；
期间并发独立Decimal CPU参考，不能与CPU35.080秒直接作正式speedup，
更不是冻结affinity/线程/配对benchmark。
原计划benchmark/long-run尚未完成；不进入Windows、不解除新RZ能力。
待审批public CUDA JENS候选仍未应用，不借本节点绕过。
