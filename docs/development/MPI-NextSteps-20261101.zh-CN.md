# MPI 下一阶段实施计划（2026-11-01）

## 已冻结的起点
实现提交 29972a1，迁移记录 cb97c71；串行基线 main 5dc6d7b。
gpu-273312 / 150.65.181.188；只在服务器开发。旧 ARCH 工作区与历史实验不覆盖。

## 阶段与验收
1. M1b：CPU 1D/2D/3D 固定 Cartesian 根网格，X 连续块带分区。本轮实施。
   验收：MPI 1/2/4 的标记场测试覆盖三个状态槽、六个原生字段、两个 species、
   内部面/棱/角、全周期、非周期、混合周期；不均匀分区，无远端影子块。
   应用验收：独立 main 串行基线 vs MPI-ON 1/2/4 和 MPI-OFF；
   2D/3D Sedov 与平滑平移；检查完整坐标、测度、字段、时刻、输出完整性；
   总池容量精确等于全局 max_blocks。继续保留 1D 既有回归和失败路径。
2. M1c：单机强扩展与双服务器实验。先记录 CPU/绑核/网络/编译器，
   预热后至少五次重复，报告中位数/离散程度、通信与计算耗时、RSS、吞吐。
   双机确认可达、版本相同和 MPI 远端启动后再运行；不把小网格计时当作扩展性结论。
3. M2：静态 AMR。先实现跨 rank 同级及粗细面，沿用现有 transfer/reflux 计划；
   检查复合网格守恒、阶段时序、分区无关性和串行一致性。
4. M3：动态 AMR。沿用现有拓扑事务、BlockHandle 和池 free-list；
   添加所有权迁移与负载均衡，验证池容量、世代、拓扑提交失败恢复。
5. M4/M5：多 GPU 与分布式重启。先确认 GPU-aware MPI 和实际硬件路径；
   再验证状态可见性、传输完成依赖、checkpoint 全域完整性和重启一致性。

每阶段通过正确性验收后才扩大范围。阶段 2–5 尚未实施，不承诺未测性能。

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
下一阶段为 M1c；AMR、多机性能、多 GPU 和分布式重启仍待实现或验证。
