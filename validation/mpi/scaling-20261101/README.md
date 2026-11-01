# MPI 固定网格测量工具（2026-11-01）

这些程序只用于实验，不链接进 ARCH，不拥有网格、内存池或求解器状态。
生产实现仍使用现有 AmrTree、GhostExchange 和 MemoryPool。

## 本轮协议

- 正式性能主机：gen-989544 / 150.65.181.189，4 vCPU。
- 双机正确性：gpu-273312 / 150.65.181.188 与上述 CPU 主机；每台最多 2 个 rank。
- 被测 MPI Release 二进制来自 b94b187；独立串行 Release 来自 main 5dc6d7b。
- 固定总网格：二维 16×8 个根块（32,768 cells），三维 8×2×2（131,072 cells）。
- SmoothAdvection，Cartesian，HLLC、MUSCL、RK3，全部活动轴周期；关闭细化、源项和 GPU。
- 每个 rank 使用一个绑定的 CPU 核，OMP_NUM_THREADS=1、OPENBLAS_NUM_THREADS=1。
- 8 步 pilot 决定固定步数；每个进程数预热一次，正式重复五次。
  每轮按预先指定顺序交错运行 1/2/4 rank；不并行运行不同样本。
- 每次保存首尾快照，按全局坐标重排后对独立 main 基线逐位比较字段、坐标、测度和时间。
- 单独运行一次通信采样诊断；正式五次计时不加载采样库。
- 原始输入、命令、日志、进程绑定、退出码、阶段计时、内存和完整输出均保留在服务器。
  Windows 仅远程控制，不保存源码或实验副本。

## 指标边界

主时间是每个 rank 的 driver_seconds - output_seconds，再取所有 rank 的最大值。
这包含驱动中的初始化、通信及等待，不能称为纯计算时间。
另外报告原生 CPU 阶段计时之和的 rank 最大值；两者分别统计，不能相加。
单 rank / 串行路径强制写最终 checkpoint，多 rank 不写，故端到端时间仅作辅助记录。

PMPI 观察器仅包裹现有 MPI_Sendrecv、MPI_Allgather、MPI_Allreduce、MPI_Bcast 调用，
不插入额外集合通信，不更改发送数据，也不改变数值算法。
MPI 调用墙钟包含等待；MPI 时间已包含在驱动/阶段时间中，不能再次相加。
sent_payload_bytes 是 API 逻辑载荷，不能当作网卡流量或总线带宽。
目前只针对本程序使用的 MPI_COMM_WORLD 与正常非 MPI_IN_PLACE 调用诊断，不是通用 profiler。

RSS 使用 /usr/bin/time 的每 rank 峰值；报告各 rank 峰值之和，
这不是所有 rank 在同一时刻的总内存峰值。
I/O、启动以及其他主机负载会影响端到端时间；双机测试只验正确性，不发布速度结论。

## 复现布局

设置 ARCH_MPI_EVIDENCE 可更换证据根目录；默认是
/home/ubuntu/projects/ARCH-mpi-evidence-scaling-20261101。
目录中需要：

- bin/ARCH：候选 MPI Release；bin/ARCH-baseline：独立串行 main Release。
- bin/mpi_fixed_grid：已有 MPI host 单元测试。
- bin/libmpi_profile.so、bin/mpi_probe：本目录 C 源码编译所得。
- tools/：本目录 Python 文件；mpi_compare.py 来自 tests/integration/test_mpi_fixed_grid.py。
- inputs/smooth.par：validation/amr/inputs/smooth_uniform80.par 的既有平滑波模板；
  inputs/sedov.par：simulation/Sedov/Sedov.par。模板由 measure.prepare 显式写入固定网格参数。
  使用 AMR 目录中的现有输入不意味着本轮开发 AMR。

工具链目录固定为 /home/ubuntu/projects/ARCH-mpi-toolchain/env。
将其 bin 放到 PATH 前面，lib 放到 LD_LIBRARY_PATH 后，用 mpicc 构建：

    mpicc -O2 -fPIC -shared mpi_profile.c -o "$ARCH_MPI_EVIDENCE/bin/libmpi_profile.so"
    mpicc -O2 mpi_probe.c -o "$ARCH_MPI_EVIDENCE/bin/mpi_probe"
    python -B "$ARCH_MPI_EVIDENCE/tools/run_cpu.py"

run_cpu.py 要求新证据目录内尚无 cpu 子目录，防止覆盖原始样本。
脚本设置了当前指定 CPU 主机名校验；变更实验主机需要明确调整协议与该校验。
不自动安装依赖或覆盖服务器现有源码。

## 双机启动

采用已验证的 Open MPI 5.0.11 同版本环境及相同哈希的二进制/动态库。
先运行 mpi_probe，确认两个主机名都出现且 Allreduce 的结果正确，再运行真实算例。
multihost-launch.json 保存本次命令；采用 ppr:1:node 或 ppr:2:node，
TCP 与运行时 OOB 均选定 150.65.181.0/24 网段。
系统 SSH 启动时移除工具链 LD_LIBRARY_PATH，防止系统 OpenSSL 被替换。

传输与启动只使用服务器间临时限制密钥，保持严格 known_hosts 校验。
run_cross.py 需要预先准备该临时授权；完成后撤销，不将密钥纳入 Git 或证据清单。
它先在两台主机分布 rank，再回收 CPU 主机分片到 GPU 主机进行完整比较。
已拒绝的启动参数和输入尝试单独留档，不计入通过数量。

## 范围

本轮不改数学库、物理模块、AMR 算法、内存池或生产 MPI 通信代码。
后续先根据测量判断优化价值；对项目现有 AMR 的 MPI 支持位于总计划最后。
