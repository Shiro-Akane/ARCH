# MPI 下一阶段实施计划（2026-11-01）

## 已冻结的起点
实现提交 29972a1，迁移记录 cb97c71；串行基线 main 5dc6d7b。
gpu-273312 / 150.65.181.188；只在服务器开发。旧 ARCH 工作区与历史实验不覆盖。

## 工作范围
本对话只负责将 MPI 落实到现有项目：进程间数据归属、通信、同步、分布式运行及其测量。
现有网格容器 AmrTree 的分区归属属于 MPI 接入点，并不意味着开发新的 AMR 算法。
此前将静态/动态 AMR、多 GPU 和重启列入后续路线，范围过宽，现已从当前计划移除。
数学库、AMR 算法和内存池自身的研发不属于本计划。

## 阶段与验收
1. M1b：CPU 1D/2D/3D 固定 Cartesian 根网格，X 连续块带分区。本轮已验证。
   验收：MPI 1/2/4 的标记场测试覆盖三个状态槽、六个原生字段、两个 species、
   内部面/棱/角、全周期、非周期、混合周期；不均匀分区，无远端影子块。
   应用验收：独立 main 串行基线 vs MPI-ON 1/2/4 和 MPI-OFF；
   2D/3D Sedov 与平滑平移；检查完整坐标、测度、字段、时刻、输出完整性；
   总池容量精确等于全局 max_blocks。继续保留 1D 既有回归和失败路径。
2. 单机 MPI 测量：在同一网格和计算设置下测试不同进程数；
   记录绑核、计算/通信耗时、RSS、吞吐与分区不均衡。预热后至少五次重复，
   报告中位数和离散程度；把启动与输出开销单独说明。
3. 双服务器 MPI：核对运行环境和 MPI 版本，验证远端启动与网络通信，
   再检查跨机器运行的数值、全局时间和完整网格是否与串行基线一致。
   性能记录必须注明异构 CPU、网络和每台机器的进程分配。
4. 根据测量改进 MPI：只有证据表明通信是瓶颈时，才评估批量传输、
   非阻塞通信或通信与计算重叠；继续使用现有交换执行器与工作缓冲区。
   每次通信改动都回归已有正确性检查。

阶段 1 已通过本轮单机正确性验收；阶段 2–4 尚未实施。
本计划不会自动进入 AMR、GPU 或其他模块的功能开发。

## M1b 具体设计
- 所有权仍在 AmrTree；rank r 持有 X 根块区间
  [floor(Nx*r/P), floor(Nx*(r+1)/P)) 及全部 Y/Z 根块。
  要求 Nx >= P；不引入空 rank、二维进程网格或新的分区管理器。
- SolverDispatch 按本地根块数预留原有 MemoryPool，再分配全局剩余槽。
  所有 rank 的容量之和必须严格等于原始 max_blocks；不改 MemoryPool 实现。
- GhostExchange 在现有 HostExchangeWorkspace.values 中打包 X 面。
  按全局 (Z,Y) 顺序标识面，不能依赖 Morton 排列的首尾或本地块 ID。
  原生 make_box / layout / field accessor 保持索引语义。
- 远端 X 交换先完成；本地 X 写入与之不重叠；原有 Y/Z 阶段将新 X ghost
  传递至棱角。无新增边角算法，无数学公式或数值核改动。
- 保留两次 MPI_Sendrecv、原有全局 CFL 归约契约与 rank-local HDF5 输出。
  暂不加入非阻塞重叠；性能测量后再决定复杂度是否值得。
- 全部活动轴只允许成对 periodic、outflow、reflecting；
  AMR、曲线坐标、源项、GPU、多 rank checkpoint/restart 继续显式拒绝。
- 文件变更限定在 topology/exchange/dispatch/入口、测试和文档；
  数学库、物理模块、transfer/refinement/storage 保持不变。

## 结果记录
原始结果保存在 /home/ubuntu/projects/ARCH-mpi-evidence-multidim-20261101。
每轮独立目录，保存命令、输入、二进制/源码哈希、退出码和比较报告。
失败结果保留并单独标记；不把旧服务器实验混为当前实现结果。

## 本轮完成状态
M1b 已通过 Debug/Release 验收：108 组应用比较、22 项 CTest、20 项拒绝检查。
详见 [多维固定网格结果](MPI-Multidimensional-20261101.zh-CN.md)。
下一步只推进上述 MPI 单机测量与双服务器运行。
