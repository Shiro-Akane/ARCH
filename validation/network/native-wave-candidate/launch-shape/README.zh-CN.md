# ODE 启动形状诊断候选

仅本机准备：尚未编译、运行，不属于生产实现，也未证明更快。
必须等待现有容量测试、批量缩放／残差 kernel 合同及其核反应门槛完成，
才考虑单独诊断。不得与服务器已有实验并行。

本轮已归档 v4 `SparseOdeBatch.cuh` 中 `advance_ode` 用
`blockIdx.x * blockDim.x + threadIdx.x` 选择独立单元，并对 `lane >= count` 返回。
Host 取 warp 宽度为线程数，故当前 1–32 单元的推进落在一个 block。
不能据此断定它占据多少耗时，或断言把 block 数增多一定更快；
更少活跃 lane 也可能降低访存合并效率、增加资源成本。

诊断只改变六个已确认的 audit150/200 × BE_NR/BD/ROS4、IdealGas
`advance_ode` 的 grid/block。使用同一批已编译 factory 对象，
不更改参数、共享标量数学、状态、矩阵、pool、stream 或同步；
其他 kernel（包括 cuDSS 和缩放 kernel）原样透传。
六个 mangled 名称来自当前服务器两 factory 对象的 `nm` 只读输出，
并与已有 API 诊断中的 150/BE 名称相符；不使用模糊子串匹配。

计划先比较同一探针下的原 32 threads 与候选 1 thread；必要时才扩展到 2/4/8/16。
启动形状仍严格覆盖每个原单元一次；不把“一个线程的 block”说成满 warp 协作算法。
探针拒绝非原始 1×32、非零动态 shared memory 或 count 超出 1–32 的匹配入口，
不增加 CUDA fence、事件或设备内存分配。没有命中计数的运行不得声称执行过候选。

数值测试仍须完整走原轨迹、真实演化和原 CPU/GPU 预算；报告 Host／GPU 资源。
两边使用同一探针，诊断计时不混入正式性能。
只有显示值得保留，才考虑把形状选择正式接入 Host 启动策略，
重做完整构建、回归和无探针配对计时。当前没有运行脚本，也没有自动发布新策略。

`source-checks-v1.log` 记录四项源码／枚举检查通过（0.005 s）：
1–32 个单元在六种线程数下恰好覆盖一次、六个精确符号、
冻结 CUDA 源身份及没有新增设备操作。这不是 C++ 编译或 GPU 验证；
冻结 `SparseOdeBatch.cuh` SHA 为
`ae3cc59770f4b952a31ded69734fa80304e06665648d40ef173551fa28edbe58`。
