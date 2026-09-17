# 小型 Jacobian sink 内联：隔离叶函数对照

2026-09-17 01:36 UTC 已在服务器单独启动，尚未得到编译／GPU 结果。
输入包 29,184 bytes，SHA `967bca9e2f7a68f0d8c42f14acf448930e58c42292ed1fa58dfabbe96a4c17a1`。
唯一 worker 302920，目录 `/home/ubuntu/projects/ARCH-native-wave-v4-20260916/leaf-inline-v1`。
此前 API 成本诊断与双端保全已经完成；不并行执行编译、实验和归档。

## 假设和唯一候选差异

实际 generated adapter 的 JacobianSink::set 是小型索引／质量缩放／能量导数累加函数，
却使用 `ARCH_HEAVY_INLINE`，在 NVCC 下明确为 `__noinline__`。
候选仅将这个函数的注解替换为 `ARCH_INLINE`；两份已固定 SHA 的 adapter 各一个位置，
可逆校验必须恢复原字节。保留全部反应数学、运算顺序、CSR pattern、网络元数据和表数据。
不将巨大 RHS/Jacobian 行函数整体强制内联，也不改全局宏、KLU/cuDSS、ODE 或温度导数公式。
输入原目录及生产生成器不修改，使用完整私有副本核对其余文件字节不变。

## 对照

- 基于原 `test_generated_network_math.cu`，保留全部 2e-10 CPU/GPU 检查；新增显式 C/O 与 uniform 两种组分、定时和完整输出快照。
- 两个实际 150/200 网络分别编译原版／候选；沿用原 CMake Release O3、sm90、NVCC 12.8、g++11 和严格浮点命令，只改私有 header、driver 与输出，并由 NVCC 链接。
- 每个组分按原版、候选、候选、原版顺序运行。每次预热 5 次，5 组 × 20 次独立叶函数；GPU CUDA event 与 Host chrono 分开记录。
- 完整比较 Jacobian、RHS、能量组分导数、温度导数和标量；要求有限、实际非零网络计算、CSR pattern 有效。同样检查原版／候选 CPU 与 GPU 向量，不只验证计时标签。
- 保持 rho=1e7、T=3e9，不改变生产燃烧输入；该叶函数没有 ODE 推进、Helm 或 cuDSS。不得用此比值代替全应用加速比。

Host chrono 测的是串行叶函数调用序列，不是完整应用的 CPU8 基线；
本阶段只比较原版／候选在同一后端上的成本，不宣称 GPU 相对八核 CPU 的加速。

所有编译串行，每个编译 wall 上限 12,000 秒、运行 300 秒；全任务 14 小时。
原 32 GiB Host 余量、64 MiB swap 增长、系统压力和 GPU 观测护栏保留，启动要求磁盘余量至少 4 GiB。
记录完整首条失败命令、time-v RSS、源码／产物 SHA 和运行输出；失败不覆盖，不放宽预算。

本机 10 项准备／协议检查通过（0.023 秒）及两项 bash 语法检查通过；
包含实际归档 adapter 的可逆单点修改、冻结命令和快照拒绝路径，**不是 C++ 或 GPU 资格**。
后续只有在结果支持时才接入生成器，并重新通过实际三 ODE、容量／长程、Helm 和正式计时。

编译等待期间补充了 5 项收集器合成拒绝测试，与原 10 项一起 15/15 通过（1.335 秒）；
不增加真实 CUDA 运行数，也未改冻结输入包。另有[原版 PTX 只读观察](baseline-ptx-observation-v1.md)，
确认动态元数据分支的存在；不据此宣称物理寄存器占用或实际加速。
