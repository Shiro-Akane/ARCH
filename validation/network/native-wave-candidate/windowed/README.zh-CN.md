# 有界工作窗口／因子 cohort 分离：仅候选准备

2026-09-17：本机准备，尚未上传、编译或运行，不属于生产优化，也没有 ODE／核反应／性能资格。
当前唯一服务器实验仍为 leaf-inline 对照；此目录不能与之并行启动。

## 改动范围

`CuDssSparseWindowSolver` 是 Host 调度封装，最多 128 个逻辑槽，仅持有一个原有
`CuDssSparseWaveSolver`（最多 32 个 native 槽）。不增加 device 分配，不修改原 provider、
cuDSS、共享数学、精度、数值门槛或 256 MiB native 估计预算。
它还没有接到 ODE 工作窗口，因此此阶段不能声称增加了实际核反应并发。

- `(逻辑槽, caller token, matrix address)` 对应独立、单调递增的内部 token；各单元可以有相同 caller token。
- 调用方修改系数必须换 token，不能以地址相同为由复用旧因子。
- 合法逻辑因子被其他页逐出后，显式重新 factorize；未知／过期／移址 token 仍报错。
- 整个窗口先检查，包括跨页的 output/input、output/output、output/CSR metadata 别名；
  Idle 但保留逻辑身份的原矩阵也不能被其他单元输出覆盖。
- 只有全部页成功才发布新逻辑身份；部分失败时清空 native 有效性，不回收已用内部 token。
- 底层物理工作统计原样返回，包括 padding 和额外 factor；上层逻辑请求、分页和逐出恢复另计。

风险是更大的窗口会失去部分因子复用、增加 native 调用和等待；不能假定它比原来更快。
详见 [源码与显存口径](../bounded-window-followup.zh-CN.md)。

## 待执行的独立合同

两个制造解维度 151、201，各覆盖 `(window, cohort)`：
`(1,1), (2,1), (3,2), (8,8), (9,8), (32,32), (33,32), (64,32), (128,32)`，共 18 组。

复用 SHA 固定的原 GPU 制造解测试辅助函数：独立已知解 `1e-12`、原 CSR residual、
输入不修改、异步传输生命周期和严格负例分类。生成头文件只截取并关闭其原 namespace，
所有辅助函数字节保持不变，不另写一套物理／误差判断。

覆盖 cold/stale/relocated/zero token、错误操作、Host/null 指针、跨页别名、空页、
tail、Factorize-only／Solve／Idle 混合、逐出恢复、重复相同 caller token、显式失效，
以及最后一页 NaN 失败后不得发布前面页新身份。基础设施错误不得算作负例成功。

构建只新增两个 Host `.o`，链接已通过合同且冻结的 batch-kernel provider；
不重编 CUDA factory、不修改归档成员。继承原 g++-11 strict-FP 配方。
运行必须在 leaf-inline 结束、审查并完成服务器／本机双端归档后，使用既有资源 guard 单独执行。
`dispatch.sh` 尚未执行，也没有排入自动后续队列。

本机的 6 项源码／配方检查及两项 shell 语法检查已通过（最近一轮 0.054 s），
日志见 `preparation-checks-v1.log`，只验证准备逻辑；C++/CUDA 合同仍全部待跑。
即便合同通过，后续仍需独立 fresh factory 接入、真实三 ODE／容量／长轨迹／Helm 及配对性能，
不能用这里的制造解替代上述验收。
