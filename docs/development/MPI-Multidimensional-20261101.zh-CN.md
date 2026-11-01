# MPI 多维固定网格结果（2026-11-01）

## 结论与范围
M1b 已通过本轮验收：CPU、Cartesian、固定根网格、沿 X 划分连续块带，
支持 1D/2D/3D。所有计算在 gpu-273312（150.65.181.188）完成。
本轮是单机正确性验证，不能据此推断多机扩展性、GPU MPI 或长时间演化稳定性。

独立串行基线仍为 main 5dc6d7bad3fcf37c1aa261d66ec1e8dcf6c5e21d；
本轮从迁移完成的 cb97c71fb83a8c5f080ff2ada1ebc20234900551 继续。
分支：codex/mpi-multidimensional-fixed-grid-20261101。

## 实现
生产代码只修改 AmrTree、GhostExchange、SolverDispatch 与 main 入口四个文件。
AmrTree 仍持有分区信息；MemoryPool 的实现与分配机制未改。
按本地根块预留容量，再分配全局 spare slots，所有 rank 的池容量严格求和为 max_blocks。

GhostExchange 沿用 HostExchangeWorkspace.values、原生盒索引和字段访问。
按全局 (Z,Y) 打包整个 X 面，修正多维 Morton 顺序下不能用首尾块代表通信面的限制。
远端 X 写入与本地 X 面不重叠，随后现有 Y/Z 阶段传播 ghost 至棱角。
没有第二套 MPI 内存池、远端影子块或独立通信管理器。

相对独立 main，src/numerics、src/physics、src/amr/transfer、
src/amr/refinement、src/amr/storage 均无变更。
所有活动轴检查内置边界和成对周期；其他既有 MPI 范围限制继续保留。

## 验收证据
| 项目 | Debug | Release |
| --- | ---: | ---: |
| CTest（MPI/交换/归约 + serial 相关测试） | 11 | 11 |
| 原有 1D 应用比较 | 26 | 26 |
| 新增多维应用比较 | 28 | 28 |
| 预期拒绝检查 | 10 | 10 |

共 108 组应用比较、22 项 CTest、20 项拒绝检查通过。
另有一个 3D Sedov 的 4-rank、2 步早期检查通过，未计入 108 组。

底层 MPI 1/2/4 测试覆盖三维度、三个状态槽、六个原生字段、两个 species、
面/棱/角、全周期/非周期/混合周期，以及交换缓存复用。
MPI-OFF Debug 和 Release 二进制均确认未链接 libmpi。

多维网格：2D 为 5×3 根块、3840 单元、全局池 16；
3D 为 5×3×2 根块、122880 单元、全局池 31。
每例完成 16 步、5 个快照。MPI 1/2/4 与 MPI-OFF 均分别对照同构建类型的独立基线。
覆盖：
- 2D/3D Sedov，RK3/HLLC/PPM、outflow。
- 2D Sedov，RK2/HLLC/MUSCL、reflecting。
- 2D/3D X 向平滑平移，RK2/RK3、HLLC。
- 2D Y 向平滑平移，RK3/Roe。
- 3D Z 向平滑平移，RK3/HLL。

比较完整三维坐标、原生单元体积、所有请求字段与全局时间的 binary64 字节。
检查根块不重复不缺失、分区区间正确、池容量总和，以及多 rank 不写部分 checkpoint。
多维新增拒绝项为不成对 Y 周期、不成对 Z 周期和全局池小于根网格。
四条候选 lane 并发最多 8 个计算进程，OMP=1、--bind-to none；
记录的墙钟耗时包含启动和输出，不能作为性能或加速比结果。

## 后验守恒诊断
直接用 DENS、ENER（原生总能量密度）乘 NativeGrid/cell_measure，
以 math.fsum 汇总全域积分。全部候选 lane 的质量与总能量积分和基线一致。
在这些已保存快照的 binary64 汇总精度下，七个算例均未检出相对积分漂移。
这不表示任意时间跨度或开放边界条件下严格零误差。

同时验证场确实演化：Debug 下最大密度变化约为 0.0103–0.5675；
逐例精确数值保存在 validation.json 与 conservation.json 中。
该诊断使用已经保存的输出，不修改数学库或求解器。

## 可复现材料
设计与后续路线：[MPI 下一阶段实施计划](MPI-NextSteps-20261101.zh-CN.md)。
机器可读结果：[验证结果](MPI-Multidimensional-20261101.validation.json)。
构建/执行命令及协议：validation/mpi/multidim-20261101/protocol.json。
通用比较入口：tests/integration/test_mpi_fixed_grid.py 与 test_mpi_multidim.py；
接受独立 baseline、candidate、serial-candidate、mpiexec 和新的 evidence 目录。
后验积分脚本：validation/mpi/multidim-20261101/summarize_conservation.py。

原始日志、输入与 HDF5 位于：
/home/ubuntu/projects/ARCH-mpi-evidence-multidim-20261101

证据清单位于：
/home/ubuntu/preservation/arch-mpi-multidim-20261101/evidence-manifest.json
清单 SHA256：08237f7d5e530db9209a74a34fb0bfb445826ab885cb034698ee84a059983197
文件数 4675，合计 3463857507 字节；最终摘要为避免循环引用不计入清单。
数据未下载到本机，也未作为 LFS 文件加入 Git。

## 下一阶段
当前范围限定为 MPI：控制绑核、网格、I/O 和重复次数，测量计算/通信耗时与 RSS，
再在两台服务器上验证启动、通信和数值一致性。性能试验先预热，再至少五次重复，
记录异构 CPU、网络条件和进程分配。必要的通信优化以测量证据为依据。
此前列出的 AMR、GPU 和重启扩展已移出当前计划；本轮也没有开发这些算法或功能。
更新后的范围见 [MPI 下一阶段实施计划](MPI-NextSteps-20261101.zh-CN.md)。
