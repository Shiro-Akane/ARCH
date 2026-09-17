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

## NVIDIA 12.8 文档核对

通过 agent-reach 的 Exa／Jina 只读核对了与当前工具链同代的归档文档，未下载到项目、
未安装或升级库。文档说明 local memory 实际位于设备内存，
warp 内线程访问相同相对位置有利于合并访问；因此不能把大 local-memory 数字
单独当作总显存或实际 spill 带宽的测量。[CUDA 12.8.1 编程指南](https://docs.nvidia.com/cuda/archive/12.8.1/cuda-c-programming-guide/index.html#local-memory)

官方通常建议线程块为 warp 大小的整数倍，也强调更高 occupancy 不一定更快，
block size 需要结合资源和实测选择。本候选的 1-thread block 刻意偏离通常建议，
仅用于区分“小批次集中一个 block”与访存／资源代价；不是建议生产默认如此。
只有完整结果证明有益才采纳。[CUDA 12.8.1 最佳实践](https://docs.nvidia.com/cuda/archive/12.8.1/cuda-c-best-practices-guide/index.html)

## 诊断归因与拒绝路径

源码复核修正了尚未运行的探针拒绝路径：不伪造一个 `cudaLaunchKernel` 返回码，
而是输出诊断错误并以 78 退出。生成的 kernel stub 可能丢弃该返回码，
仅返回错误并不会设置 CUDA 自身的 last-error 状态，可能漏掉一次实际未发出的 launch。
正常路径仍原样返回真实 CUDA 调用结果。旧准备版本没有上传或运行。

另有 `prepare_wait_probe.py`：在已归档 API 观察器的两个可逆位置添加标签，
区分 D2H 等待前最近观察到的 ARCH launch，避免把小量状态回读的等待全部说成传输带宽。
不增加 CUDA 调用、事件或 fence，也不改变启动形状；不追踪 cuDSS 内部 kernel，
标签不能解释为精确的 GPU kernel 耗时或唯一因果归属。
该观察器仍未编译／运行；准备后源码 SHA 为
`c046281beb191b5a3f00ce62c41b17880c9a71ae15e3cad5ce9c3349708ab7d2`。
`diagnostic-source-tests-v2.log` 记录修正后六项源码／准备测试通过（0.003 s），
包括等待标签只改两个可逆位置及拒绝未审阅基准；仍不构成 C++ 或真实 GPU 资格。
