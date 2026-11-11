# MPI 执行预检、自动回归与扩大实验

本轮承接固定网格 MPI 工作，基于 upstream main 2e05e66e（v1.4.1）和已有 MPI 提交 c0dbf06e。使用 MPI/execution-preflight 分支。全部开发、构建、实验和原始证据保存在 gpu-273312 服务器。

## 三步与范围

1. 将 MPI 启动约束并入现有 ExecutionPreflight，让查询和实际启动共用判断。
2. 增加 MPI ON/OFF 自动回归，并把 MPI 作业接入 CI required。
3. 审阅架构和回归证据，在固定网格上扩大到 1/2/4/8/16 进程。

AMR 放在上述工作之后；当前多进程仍拒绝 AMR、源项、GPU 和 checkpoint/restart。本轮不实现这些能力。

## 实现与审阅

--check-run 增加 --mpi-ranks 参数，默认 1。查询仍在一个进程中执行，正式运行读取实际 communicator 大小；物理输入格式不变。identity.mpiRanks 绑定计划进程数，data.mpiEnabled 报告构建能力。它们是接口 version 1 的增量字段，现有单进程字段保持原义。Studio 仍按单进程启动，此接口增量不等于 Studio 已支持 MPI。

固定网格、X 分区、边界配对、全局块预算、源项和 checkpoint/restart 的判断由现有 dispatch/ExecutionPreflight.cpp 负责。BackendCapabilities 负责 MPI 的 CPU 后端限制；即使存在可用 CUDA 设备，Auto 也会给出选择 CPU 的明确理由，显式 CUDA 请求则失败。多进程预检不加载不适用的 GPU 运行库。

main 继续负责 MPI 初始化、输入一致性和错误终止。Dispatch 在共享预检成功之后才创建输出/日志目录；原有 log_dir 回退规则不变，相应架构审计定位到新位置。内存池的全局容量与原分摊公式保持不变，已有拓扑和交换工作区继续拥有数据。

审阅边界：src/core、src/numerics、src/physics、src/amr/storage 和 src/amr/transfer 与 main 没有差异。没有增加 MPI 管理层、独立分配器或第二套时间推进器。MPI 通信内核也未在本轮变更。

## 自动回归

GitHub CI 的 MPI fixed-grid 作业构建隔离的 ON/OFF 程序，执行预检、后端能力夹具、1/2/4 进程 Host 测试和 1D/2D/3D 完整演化对照。MPI 与 mpi-off 检查配置有独立覆盖锚点，缺少测试、漏报、跳过或失败都不能通过。MPI 作业参加 CI required。

预检回归包括无输出副作用、MPI 关闭、计划进程数绑定、非法/重复 CLI 参数、Auto/显式 CUDA，以及预检与真实启动拒绝理由一致。原数值回归继续覆盖不均匀分区、紧内存池、边界、输入不一致及单 rank 读取失败，并补充真实 Auto 演化。

托管 CI 允许进程过量分配以验证正确性，其运行时间不能作为扩展性能数据。只上传诊断、输入和摘要，不上传 HDF5 或构建产物。

## 服务器验收与性能

四种 Debug/Release × MPI ON/OFF 构建完成，22 项相关 CTest 全部通过。独立串行基线的 110 组演化输出对照全部逐位一致，20 个预期失败场景全部正确退出。工具套件 629 项全部通过且没有跳过；架构、文档和工作流语法检查通过。最初工具套件曾因服务器 Python、HDF5 和编译器混用失败，修复仅位于服务器独立工具目录；失败记录和最终通过记录均保留，没有放宽测试或改动生产依赖。

代码提交为 e43183d0，功能提交为 9054a353。[代码 PR #25](https://github.com/Shiro-Akane/ARCH/pull/25) 的 10 项检查全部成功，包含新增 MPI 作业、原有 CPU 全量测试、Tooling、Studio 和 CodeQL；无合并冲突，保持 Draft。本轮未合并 main。代码分支已经推送到 Shiro-Akane/ARCH 和 Arsenic-er/ARCH 的 MPI/execution-preflight。

扩大实验使用同一份已验收的 Release 程序，源码内容与 e43183d0 的一致性通过冻结差异哈希复核。复现协议为 validation/mpi/controlled-20261102/scale-up-protocol.json，执行器沿用原 run.py 的 scale-up 入口及原始逐位比较、计时、核绑定和归档校验。二维网格 131,072 单元、128 步，三维 524,288 单元、48 步，单元数均为上一轮对应强扩展网格的 4 倍。

共 62 次运行：2 次独立串行基线，10 次预热，50 次测量。60 组分布式输出对照全部逐位一致，初末时刻、字段、单元覆盖和每 rank 完成步数均被检查。每个进程数保留全部 5 次正式样本，顺序交替，不删除慢样本。下表为 max_rank(driver_seconds - output_seconds) 的中位数；加速比相对于同一 MPI 程序的 1 进程中位数，不是端到端作业加速比。

| 进程数 | 二维秒数 | 二维加速比 | 三维秒数 | 三维加速比 |
| --- | ---: | ---: | ---: | ---: |
| 1 | 31.293 | 1.00× | 63.666 | 1.00× |
| 2 | 15.675 | 2.00× | 31.967 | 1.99× |
| 4 | 8.254 | 3.79× | 16.377 | 3.89× |
| 8 | 4.472 | 7.00× | 8.692 | 7.32× |
| 16 | 2.702 | 11.58× | 4.932 | 12.91× |

16 进程的效率分别为 72.38% 和 80.68%。二维 16 进程范围为 2.49–2.98 秒（CV 7.83%）；三维为 4.55–6.18 秒（CV 13.92%）。共享虚拟机和运行窗口会影响时间，不据此声称统计显著或多节点加速；本轮也没有测弱扩展。JSON 保留每次样本、rank 核绑定、资源数据、中位数、范围和 CV。sum_rank_peak_rss_kib 是各 rank 峰值之和，不是同步测量的整机峰值。

62 份扩大实验归档全部校验 SHA-256；每个归档中的全部成员在移除临时输出前已逐项读回核对。回归输出另有 4 份归档，完成逐项哈希和代表性 HDF5 恢复检查。RAM 临时目录已清理，原始证据在服务器保留；Git 只保存协议、代码、摘要和校验信息，不新增 LFS 对象。

详细记录见 [机器可读验收与实验结果](MPI-Preflight-20261102.validation.json)。实验记录位于 MPI/scaling-16，基于已验收的 MPI/execution-preflight，运行时代码没有额外差异。


证据目录：
- /home/ubuntu/projects/ARCH-mpi-evidence-preflight-20261102
- /home/ubuntu/projects/ARCH-mpi-evidence-scale-up-20261102

本轮扩大实验是共享虚拟机上的单服务器强扩展实验，不替代既有双服务器实验，也不证明 16 节点或生产集群性能。
