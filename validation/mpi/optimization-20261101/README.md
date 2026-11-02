# MPI 通信优化与扩展验证

本轮对应用户要求的前两步：排查 rank 等待差异、扩大进程规模，再完成有证据支持的 MPI 通信优化。
AMR 兼容支持仍在计划末尾，本目录不实施 AMR 算法。

## 唯一生产改动

DriverUtils.h 中的 reduce_block_minimum 原本每次创建三个接收 vector，
依次进行 double / int64 / uint64 三次 Allgather。
现在将同一组字段明确编码为十二个 uint64 word，一次 Allgather 后恢复原有字段。
binary64 数值使用 bit_cast，不做数值转换；有符号逻辑键同样按位编码。
每 rank 的逻辑载荷仍为 96 字节，变化是集合通信次数 3 → 1，以及临时接收分配 3 → 1。

原生 combine_state、finalize_reduction、按 rank 顺序合并的规则均保持不变。
保留负零决胜、NaN 忽略、无穷值、空局部分区及全局空输入拒绝。
没有新的 MPI 数据类型缓存、通信管理器、内存池或生命周期抽象。

## 已冻结的前态

- 代码：9d3914a68d6db72c06212f8c1a528d477087e97b。
- 被测生产前态实际代码：b94b187；9d3914a 只新增此前测量材料。
- 独立串行基线：main 5dc6d7b。
- 原始目录：两台服务器均为 /home/ubuntu/projects/ARCH-mpi-evidence-optimization-20261101。
- 工具链：/home/ubuntu/projects/ARCH-mpi-toolchain/env。
- 编译前态微基准后才修改生产头文件；前后微基准编译命令保持一致。

## 实验入口

- reduction_bench.cpp：直接调用生产 reduce_block_minimum，1000 次预热，五次样本；
  每次样本内部连续运行 10000 或 20000 次调用，样本边界才使用 Barrier 和 Reduce。
  报告所有 rank 的最大时间，不把边界同步计入循环时间。
- run_placement.py：固定字段与网格，正常/反向/轮转绑定各重复三次。
  此诊断旨在观察等待是否稳定跟随某个 rank 或核心，不能单独证明硬件瓶颈。
- run_paired.py：空闲的 gen-989544 上分别交错运行 before/after，2/4 rank，
  每版本每进程数预热一次、五次正式重复；另跑不计入主统计的 PMPI 诊断。
- run_regression.py：Debug/Release 的 CTest、8/16 rank host 测试及既有 1D、多维应用比较。
  MPI-OFF 和独立 main 比较使用此前冻结的已验证二进制；本次代码变化全部位于 MPI 条件编译分支。
- run_expanded.py：gpu-273312 的 16 个选定 CPU 核，1/2/4/8/16 rank，
  一次预热、五次正式重复。该主机仍有其他项目，必须作为共享主机结果解读。
- mpi_trace.c / run_aligned_trace.py：同一主机 CLOCK_MONOTONIC 的逐次进入/退出时间，
  分解集合调用的到达差和最后一个 rank 进入后的完成尾段；只用于诊断，不用于正式性能数值。
- run_cross_optimized.py：前后两台服务器的候选二进制一致后，验证 2/4 rank 的二维、三维输出。
  远端启动使用限制范围的临时 SSH 授权，作业结束即撤销。

复用 validation/mpi/scaling-20261101 的 measure.py、rank_wrapper.py、
mpi_profile.c 及已有 integration 比较函数。
measure.launch 新增可选 executable 参数，避免切换或覆盖已冻结的基线二进制。
rank_wrapper 记录所绑定 CPU 核在作业前后的 /proc/stat 计数及 steal time。
全局启动与输出记录仍单独保存，主比较区间保持 max_rank(driver_seconds-output_seconds)。

## 验收边界

所有比较必须检查完整坐标、测度、保存字段与时间逐位相等，并检查实际步数与绑核。
计时采用分布与配对结果，微基准改善不能等同于整场模拟改善。
PMPI 时间包括等待，逻辑载荷不是网卡流量；各 rank 的峰值 RSS 之和不是同时内存峰值。
保留失败尝试、全部重复和原始 HDF5，不按结果好坏删除样本。
统计中的共享核心负载与主机限制应写入报告，不将本轮结果外推为 32 rank 或集群性能。

全部开发、构建和数据存储在服务器完成。只有脚本、文档和小型 JSON 进入 Git，
不新增 LFS 科学数据，不向 Windows 下载源码、二进制或完整实验目录。

## 扩展试验的存储调整

准备扩展阶段时，其他项目的备份下载使 GPU 节点磁盘低于此前的 24 GiB 预留值。
因此正式扩展阶段在服务器 /dev/shm/arch-mpi-expanded-20261101 保存输出，
二进制、工具和输入通过指向现有服务器证据目录的符号链接复用。
二维为 32×8 根块、64 步；三维为 32×1×1 根块、32 步。
选定 CPU 核为 0–9、16、17、18、20、22、23。

每次输出比较与测量结束后，将该次完整目录无损压缩并直接传到 CPU 服务器：
/home/ubuntu/projects/ARCH-mpi-evidence-optimization-20261101/expanded-archives。
压缩包总预算 200 MiB；CPU 服务器仍保留至少 16 GiB 磁盘空间。
expanded-archive-index.json 记录压缩包 SHA256 和每个原始文件的大小及 SHA256。
最终逐成员校验压缩包，确认日志、输入、HDF5 和计时记录完整可还原。
压缩及传输不包含在各次应用计时内；输出介质变化必须与共享主机条件一起注明。
不能把这些扩展结果与上一轮磁盘输出的数字直接作为 before/after 对比。

归档可用 verify_archives.py --evidence <CPU 证据目录> 逐成员重新核验，无需解压到磁盘。
