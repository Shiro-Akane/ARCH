# 大网络工作区：本地源码定位，不是新的 GPU 测量

2026-09-15。当前服务器的正式计时队列保持不变。本页不授权改变物理公式、
导数精度、NSE 开关、ODE 接受准则或编译浮点设置，也不表示已完成工作区优化。

## 已有实测能说明什么

[原 V1 探针](../../backend/results/hpc-cuda-optimization/V1-residual-20260914/README.md)
在同一二进制符号上观测到 `advance_ode<audit150, BD, IdealGasView>` 的
`localSizeBytes=47152`、255 个寄存器；微测试最大 launch 是 1 block × 32 threads。
这说明检查局部存储和有效并行度有价值，但不能直接推算整设备显存归属、spill 次数、
耗时占比或完整 Helm 应用的性能。更不能把 150 的单个 ODE/EOS 属性推广到全部网络。

## 当前源码给出的边界

| 区域 | 当前存储／调用事实 | 下一步要确认的事 |
|---|---|---|
| `ode_bd.h::Continuation` 的 extrapolation tableau | tableau 是 continuation 成员；`CudaBackendBurnSparseImpl.cuh` 的 `contexts_` 由有界 `DeviceAllocation<Context>` 持有，按 lane 使用。不是源码中每次调用新建的局部 tableau | 不应因数字接近就断言 47 KiB 来自这个 tableau。需查看同一产物的编译器资源记录和实际调用 |
| `odeFunction.h::assemble_burn_jacobian` | 有 energy/RHS/cv/Hessian 等按物种数量的临时数组，并调用完整共享 first-law 数学 | 确认存活区间、调用帧和访存成本；不能把所有声明大小简单相加当实际峰值 |
| `NetworkDerivative.h::temperature_derivative` | 保留完整 RHS 的四阶差分和边界前向 stencil，使用 upper/lower、RHS 临时数组 | 如调整存储，只改变工作区所有权；不能改成较低阶导数或减少 RHS 评估次数 |
| `BurnThermodynamics.h` | EOS 未提供解析接口时，gradient/Hessian-action 走共享数值回退，含嵌套临时工作区 | 区分当前 EOS 的实际实例化路线，不假设 Helm 一定走全部 fallback |
| `integrate_nse_state`／`NSESolver` | 有 candidate/log-composition 等临时数组；生产保留 NSE 分支 | 需要区分运行时调用和编译保留的路径；不能关闭 NSE 来降低资源并宣称等价 |
| 生成网络的 `PortableCxx.py` | 已有 major-stage `ARCH_HEAVY_INLINE` 调用边界、逐 Jacobian 行分拆、值型 rate storage 复用 | 不重复实现已有优化；必须针对固定生成包的实际代码和资源，而非任意更换生成器／上游库 |
| sparse pool 与跨块调度 | pool 容量受单 warp／内存约束；当前 Host 控制逐块调用，同一 provider 的 lane 调度仍决定工作批次 | native wave 候选先解决提交粒度；不同时扩大 pool、改块映射和改物理工作区，以免无法归因 |

源码入口：

- [ODE continuation](../../../src/numerics/burnsolver/ode_bd.h)
- [稀疏池所有权](../../../src/cuda/runtime/burn/CudaBackendBurnSparseImpl.cuh)
- [共享 RHS／Jacobian](../../../src/numerics/burnsolver/odeFunction.h)
- [完整 RHS 温度导数](../../../src/numerics/burnsolver/NetworkDerivative.h)
- [共享热力学导数](../../../src/numerics/burnsolver/BurnThermodynamics.h)
- [生成器的既有边界适配](../../../tools/network/PortableCxx.py)

## 后续试验顺序

先完成原冻结验证与 native-wave provider／ODE／全应用合同。若批处理后仍需优化工作区，
在单独、无计时竞争的窗口记录同一产物的 kernel attributes、编译器函数资源和受控进度/API 诊断。
诊断版本不混入正式样本；API 等待时间不直接相加当 GPU kernel 耗时。

只有定位到具体活跃工作区，才试验显式、有界、逐 lane 的 scratch 所有权。
数学继续来自同一标量函数，旧／新路径用同一状态、原三 ODE、150/200、真实 EOS、
池尾部／更换、失败传播及完整应用对照。内存低水位改善不自动等于速度提高；
必须同时报告实际显存、额外全局访存、原请求数与额外工作、端到端耗时及数值合同。

当前本页没有新增 kernel 属性测试、编译或生产代码更改。
