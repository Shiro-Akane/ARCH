# 稀疏求解批量 kernel 候选

状态：仅本机准备，尚未编译或在 GPU 上验证；不属于生产实现，不宣称提速。

## 依据与边界

native-wave v4 将 cuDSS 调用改为批量，但源码仍逐 lane 启动行／列缩放、RHS 缩放、
解缩放及原系统残差 kernel。小批次实测的 kernel 数没有下降；这足以提出合并启动的
试验，但不能据此断定这些 kernel 占据总耗时的多少。

本候选保持 v4 的 cuDSS 调用、完整 cohort 分解策略、缓存 token、修正次数及
CPU/GPU 数学不变。五类 kernel 改成一次提交最多 32 个矩阵，每个矩阵独占一个 CTA；
仍调用原 `LinearEquilibration.h` 和 `SparseResidual.h` 的标量函数。修正量加法继续
使用原 `accumulate_sparse_correction`，没有复制该公式。原矩阵、RHS 不得写入；
inactive 和 factor-only lane 不得写物理解。

描述符按值传入 kernel，编译期限制不超过 3 KiB；不引入 device 动态分配、
额外 stream fence、persistent 全局缓存或超出原 1–32 范围的 pool。
一个 CTA 的 thread 0 清除该矩阵的状态，CTA barrier 后其余线程才写入状态；
同一矩阵不能分配给多个 CTA，否则此清零约定不成立。
逐行和逐列内部求和／遍历次序不变。超过 256 行通过同一 CTA 的 stride 处理。

统计中的 kernel 数对应实际启动次数；原传输计数仍不含 CUDA kernel 参数的隐式传递，
不能将这个计数当成完整 PCIe 流量。

## 必须执行的门槛

1. `prepare.py` 只替换 provider 的三个可逆区域，并验证 v4 原始字节 SHA。
   ABI header 不变；准备工具通过不等于编译通过。
2. `test_wave_kernels.cpp` 对原／新 CUDA kernel 做逐字节比较：
   extent 1/151/201/513，容量 1/2/8/32，全部／部分 active，正常／大尺度差／NaN／Inf。
   比较全部私有缓冲、物理解和状态，检查 inactive 哨兵与原输入不变，共 256 个组合。
   该测试不是独立物理 oracle，也不是 sanitizer。
3. 重跑 v4 的 151/201 × 容量 1/2/8/32 provider 合同，包括因子复用、初始空槽、
   混合 factor-only、失效恢复、别名／主机指针拒绝及负例失败分类。
4. 使用本轮已完成的新 factory 对象，真实 150/200 × 三 ODE，小批次及 32→33 容量轨迹；
   保持原物理时长、步数、预算、EOS 和库。仍需真实 Helm 应用、长程及配对正式性能。
5. 若新路径失败，保存首条完整命令及日志；不更改物理合同来获取通过。

当前服务器的 v4 capacity-v2 运行结束并双端保全以前，不启动此候选的构建或 GPU 测试。

补充：测试启动时查询五个新 kernel 的寄存器、静态 local/shared memory 和最大线程数，
用来检查按值描述符是否带来额外静态局部存储。这不是实际 spill 流量或耗时测量；
生产 provider 不调用该查询。当前没有这些资源的真实测量结果。

已准备串行控制配方：完整 v4 capacity → 归档／本机全字节核验 → 独立候选编译／合同。
任一容量失败或保全失败都会阻止后续构建；不自动重启失败的服务器实验。
冻结候选输入包为 68,096 bytes，SHA
`9fcfb256409b6591cc5e00bb04ea22748a1de29e869403621ccb257bc3609e25`。
其中改动的 provider SHA 为
`0b478540e0b736bc7f201657341f16222bfd3d42351ce0031f4bcf3337bcc865`，
公开 ABI header 与已编译 factory 相同。
