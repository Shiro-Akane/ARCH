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
