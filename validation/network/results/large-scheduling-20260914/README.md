# 大网络 BE 调度诊断：失败保留，固定页试验不采用

本目录不是数值或性能验收。生产代码仍为已发布的 `8da9b23d`；以下固定页变动只存在隔离 overlay，未并入生产。
观察器插桩与其他编译并发的样本不进入正式计时中位数。

## 原长轨迹仍然超时

使用相同的 audit150、BE_NR、32→33 单元存储、8-lane 池、16 步／1e-9，
保持 rho=1e7、T=3e9、cv=1e8、rtol=1e-7、原字段／limiter 预算与 1800 秒整次运行上限。
CPU 仍是原 KLU；CUDA 仍是有界 factor-cache v2。只增加测试性原生 API 观察器。

该完整命令再次超时；audit200 和 33 单元存储尚未启动。
日志中的首个 32 单元宏步 CPU 为 82.0700 秒、GPU 为 1180.3019 秒，但这不是正式加速比样本。
最终累计快照已有 190,000 次 native factorization、190,098 次 solve，symbolic analysis 仅 66 次。
资源 guard 没有终止任务、没有 swap 增长；GPU 峰值 11,006 MiB，剩余最低 8,757 MiB。
因此不能将这次失败写成 OOM，也没有证据支持把主要问题归咎于反复 symbolic analysis。

Host API 的累积时间不是 GPU kernel 时间，尤其不能将分解、同步和设备运行时间直接相加推导加速上限。
原 1800 秒失败没有被后来的小规模通过或下面的 300 秒诊断覆盖。

## 状态回传与固定页隔离试验

额外两个 300 秒观测使用相同的原输入、同一个 capacity-selectable Host 对象和冻结的 device factory。
这是明确截断的执行前缀，**不作为完整轨迹资格**。
第二次只重编／重链接私有 provider：每个有界 factor owner 的 8-byte Host 状态缓冲区改用
`cudaMallocHost`，保留同步、cuDSS DATA_INFO、原系统残差、错误传播和退休顺序。
原生产文件、ODE、EOS、网络和旧二进制均未覆盖。

两次最后一个完整快照都是 110,000 次显式 kernel launch：

| 同一调用数量快照 | 原状态缓冲区 | 固定页试验 |
|---|---:|---:|
| D2H 调用次数 | 44,548 | 44,548 |
| D2H 总字节 | 425,376 | 425,376 |
| D2H API 内累积秒数 | 178.2450 | 163.5051 |
| residual 后显式同步 API 累积秒数 | 0.0128 | 14.4843 |
| column normalization 后同步 API 累积秒数 | 0.0167 | 0.2342 |

主要观察是等待位置转移，并未显示值得采用的端到端收益；故不采用该候选。
不能将 D2H API 时间的下降单独宣传为优化成功。这也不证明不存在其他有效的有界批量调度方案。

候选的 Host residual／真实 CUDA provider 合同均通过，但完整大网络数值门槛没有运行，
合同通过不代替数值资格。第一次构建尝试被 provenance 拒绝，原因是 source-root 与冻结 build 不符；
更正为该 build 的真实源码根后，在新的输出目录完成构建，保留首次失败日志。

## 可恢复证据

`evidence/records/` 保存原超时、两次 API 前缀、所有输入／命令／观察器源码、
固定页候选源码、provider 合同、构建身份与原生编译／链接命令。
完整包额外保留实际可执行文件和 provider 库，服务器与本机均已校验 SHA：

- raw：`large-scheduling-diagnostics-v1.tar.zst`，18,132,599 bytes，80 个源文件／结果文件。
- raw SHA-256：`e7c74d4d3892d62a44efc39b62e76d98d46e4d788ea4583fb38be59cf7889164`。
- compact：187,555 bytes；SHA-256 `9204cb57283baed0a8063386dfb707cd416e10747af7d1ff64ff26a38ab6e372`。
- 服务器 `/home/ubuntu/projects/ARCH-microphysics-20260914/build/`；本地 `C:/tmp/ARCH-perf-20260909/build/`。

150／200 核素的完整 ARCH＋Helm 应用验证单独进行；不能把本目录的部分执行结果外推成全应用验收。

## 正式结果闭合后的只读结构核对

完整应用的后续 [正式六算例结果](../large-application-20260914/formal/summary.zh-CN.md)
已通过 144 次运行／138 次字段与宏步、regrid 比较，但原 32 物理单元短算例的 CUDA 耗时
仍为最快 CPU8 的约 5.0–10.3 倍。以下是对同一生产代码的结构核对，不是新的 GPU profiling，
也没有在正式采样时改变源码或二进制。

- [运行控制层](../../../../src/cuda/runtime/control/CudaBackendMicrophysicsControl.cpp#L299) 在块循环内调用稀疏 owner；与 DenseLU 的跨块 bindings 不同，当前稀疏路线没有把多个块的单元一起提交给该 owner。
- [稀疏 owner](../../../../src/cuda/runtime/burn/CudaBackendBurnSparseImpl.cuh#L79) 的容量取首个块 active cells、硬件 warp 宽度和可容纳的 lane 数的最小值。因此“两块合计 32 个物理单元”不能直接写成“生产程序同时执行一个 32-lane 批次”；实际每批还受块内单元数限制。
- [ODE continuation 调度](../../../../src/cuda/microphysics/SparseOdeBatch.cuh#L213) 先下载一批请求并等待，然后在 Host 的 lane 循环中逐个调用 factorize／solve。已有有界因子缓存不等于已经实现多矩阵批量求解。
- [provider 完成边界](../../../../src/cuda/microphysics/CuDssSparseSolver.cpp#L161) 在每次原生执行后检查设备状态并同步；solve 还保留原系统残差检查及有界修正。往返主要是请求和状态整数，不能将其描述成每次把完整矩阵或 RHS 搬回 CPU。一次性的 CSR 元数据读取也不等于 CPU 数值求解。

由代码结构与上述观察可推断，下一项可测量的执行层目标是减少逐块、逐 lane 的串行提交与完成等待，
而非仅把状态缓冲区固定页化或无限扩大因子缓存。已有
[non-uniform API 探针](../../../../validation/network/probe_cudss_nonuniform.cpp) 和
[32 系统原始日志](../../../backend/results/hpc-cuda-optimization/P12-kernel-batch-20260914/evidence/nonuniform-batch-32.log)：
在现有 cuDSS 0.8.0.10 上保留 BTF_COLAMD、GPU-only 和 IR=2，1／2／8／32 个制造解系统已通过，
最大相对误差 2.40862e-16。这不是实际核反应矩阵，更不覆盖自适应 ODE。
因此接口能力并非完全未知；尚待独立候选验证的是把它接入有界多 lane 请求、因子复用、
原系统残差、失败隔离及生命周期约束后的正确性与端到端收益。这里不宣称该接入已经实现或必然加速。
任何候选仍须保留同一 ODE、矩阵 token／世代、残差、错误传播和内存／退休边界，
不通过减少核素、改库、换 CPU fallback 或放宽预算来获得“通过”。

原 BE 大容量长轨迹另已准备 [只延长 wall 护栏的补测配方](prepared-extended-wall/README.md)，
须等全部耦合正式计时及双端备份完成后再执行。目前只是准备检查点，原超时结论没有被改写。
