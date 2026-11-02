# MPI 固定网格实施记录

本文保留首个 1D 切片的历史记录。后续多维设计与阶段计划见
[MPI 下一阶段实施计划](MPI-NextSteps-20261101.zh-CN.md)。

基线：上游 `main`，`5dc6d7bad3fcf37c1aa261d66ec1e8dcf6c5e21d`。
`build/win11` 已通过 PR #21 合并；本实现按用户后续指示直接基于 main。

## 当前边界

这是 M0 与 M1 的首个可验证切片：CPU、一维笛卡尔根网格、无源项流体。
`ARCH_ENABLE_MPI` 默认 OFF。ON 后由 `mpiexec -n N` 指定进程数；每个进程至少一个根块。
Euler、RK2、RK3 继续使用原积分器。成对周期、outflow、reflecting 边界走原边界处理。
多进程下拒绝单侧周期配置，避免没有对应远端面的 ghost 区被当作有效输入。
多进程下禁止细化网格、CUDA、燃烧、扩散、引力、自定义边界和 checkpoint/restart。
这不是完整 MPI/AMR、多节点或性能扩展验收；Windows/MS-MPI 和 Release 构建尚未实测。现有单进程能力不受这个多进程边界限制。

## 集成位置与所有权

- `AmrTree` 保存 rank/size 和全局根块区间，区间为 `[floor(B*r/P), floor(B*(r+1)/P))`。
  全局几何和逻辑坐标不变，`GetActiveBlocks()` 返回本进程拥有的块。
- `SolverDispatch` 先为本地根块保留 slot，再分摊原 `max_blocks` 的剩余容量，仍由原 `MemoryPool` 和 free list
  分配三个状态槽。各 rank 容量之和严格等于原全局预算。没有远端块副本、第二个池、MPI allocator 或通信管理类。
- `GhostExchange` 仍是唯一 Host ghost 完成入口。远端 X 面借用现有
  `HostExchangeWorkspace::values`，复用 `ExchangePlan` 的盒子、字段和布局索引。
  只传 6 个原字段和 species，完成后原本地交换计划继续执行。两次 `MPI_Sendrecv`
  使用不同方向 tag，支持两进程周期环和不均匀分块；物理边界使用 `MPI_PROC_NULL`。
- Pool index 和 BlockHandle 仍是本进程身份，不在网络上传递。远端关联依赖全局根坐标。
- CFL 在 Driver 的块归约入口汇总各进程候选，调用原 `ReductionSpec` 合并与终结逻辑。
  NaN 忽略、逻辑坐标平局选择和全局空集拒绝均保留。不用进程局部和替换顺序守恒求和。
- 主入口申请 `MPI_THREAD_FUNNELED`；OpenMP 团队结束后才进行 MPI 操作。
  参数文件内容与 problem 名不一致会拒绝启动。任何进程异常通过 `MPI_Abort` 结束作业。

`src/numerics`、物理公式、EOS、重构、积分器、AMR 插值和容差均不修改。

## 输出

多进程输出位于原输出目录下的 `rank-0/`、`rank-1/` 等子目录；日志目录也按 rank 分开。
路径变化在 I/O 层处理，不改写已校验的科学配置。`partition.txt` 记录全局/本地根块区间、
池容量和 rank-local 范围。每个 HDF5 是一个 shard，不可当作完整网格。
当前时间、修补量和边界收支日志中的局部统计只代表本 rank；需要按全局逻辑顺序重组后分析。

MPI 路径不生成串行格式的部分 checkpoint。显式启用 checkpoint/restart 会在启动时被拒绝，
原 Driver 的隐式初末态 checkpoint 调用在多进程时跳过，PLT 输出保留。

## 构建与验证

```sh
cmake -S . -B build-mpi -DARCH_ENABLE_MPI=ON
cmake --build build-mpi -j2
ctest --test-dir build-mpi -R 'mpi_fixed_grid|same_level_exchange_plan|reduction_contract' --output-on-failure
mpiexec -n 4 ./bin/ARCH Sod simulation/Sod/Sod.par
```

MPI C++ 开发包和原有 HDF5 等依赖需要可发现。MPI 关闭时不查找、不链接 MPI。
普通 HDF5 仍可使用，因为每个 rank 写自己的文件；本阶段不新增 parallel-HDF5 路径。

`tests/host/amr/test_mpi_fixed_grid.cpp` 在真实 1/2/4 进程下检查原池与交换入口，覆盖：
不均匀块数、非周期/周期、三个状态槽、六个原字段和两种 species、缓存复用、
全局 CFL、signed-zero 平局、局部 NaN 空集和全局空集。

`tests/integration/test_mpi_fixed_grid.py` 接收独立构建的未修改 main 和候选程序：

```sh
python tests/integration/test_mpi_fixed_grid.py \
  --baseline /server/baseline/bin/ARCH --candidate /server/candidate/bin/ARCH \
  --serial-candidate /server/candidate-with-mpi-off/bin/ARCH \
  --evidence /server/new-evidence-directory
```

它按全局坐标拼接每次输出，检查字段、几何、全局时间、无丢失/重复根块及二进制值一致性；
覆盖 16 步、每步输出的 Sod 与周期平流，含非零初速度的反射边界、MPI+OpenMP、
可选的 MPI-OFF 程序对照、5 进程下 7 个根块共用 8 个 pool slot 的紧容量分配，
以及未支持配置的快速失败。它是短程正确性回归，不是长时间稳定性或性能证明。证据目录必须不存在，避免覆盖失败记录。
本次服务器构建使用 Debug、GCC 14.4.0、Open MPI 5.0.11、CMake 3.31.8，
关闭 KLU 和自定义反应网络；MPI-ON/OFF 与独立 main 基线使用相同工具链及 HighFive 源码。
本次 Open MPI 的 5 进程紧容量检查显式使用 `--mpiexec-arg=--oversubscribe`；
该参数由验证脚本透传给 launcher，可在有足够 slot 的机器上省略，不能据此判断加速比。
具体执行结果另见同目录 [MPI-FixedGrid.validation.json](MPI-FixedGrid.validation.json)；未实测项目不算通过。

本次实际结果：MPI-ON 的 5 项 CTest、MPI-OFF 的 6 项 CTest 全部通过；
20 组 MPI/混合线程对照及 6 组 MPI-OFF 对照均与独立 main 基线逐位一致，
每组 17 次输出；7 类无效/未支持配置按预期退出。
5 进程、7 根块、全局 8 个 slot 的实测容量为 [1, 1, 2, 1, 3]，总和为 8。
完整原始输出保留在服务器；JSON 记录二进制及最终源码哈希和早期失败记录。

## 对 ARCH-MPI 初步计划的调整

1. 先完成可复现的一维固定网格切片，保留原数学与状态生命周期，不先搭通用 MPI 框架。
2. M1 后续扩展二维/三维时，将远端通信接到原 X/Y/Z 阶段，专门验证棱角传播；
   不把本阶段的一维正确性直接外推到多维。
3. M2 静态 AMR 在原粗细交换与 flux register 内处理跨 rank 所有权，再验证守恒。
4. M3 动态 AMR 将块迁移纳入原拓扑事务及池 slot 生命周期，不新增一套分配/回滚系统。
5. M4 CUDA 在原 backend storage/residency 所有者内处理通信缓冲可见性和完成顺序，
   验证实际 MPI 的 GPU buffer 支持后再决定通信路径。
6. 全局 checkpoint/restart、分布式诊断与多节点性能分别验收。

实现参考：[MPI_Init_thread](https://docs.open-mpi.org/en/main/man-openmpi/man3/MPI_Init_thread.3.html)
与 [MPI_Sendrecv](https://docs.open-mpi.org/en/main/man-openmpi/man3/MPI_Sendrecv.3.html)。
