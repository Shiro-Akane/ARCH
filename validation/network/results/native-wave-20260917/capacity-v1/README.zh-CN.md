# Native-wave v4：首轮容量矩阵超时，未通过

2026-09-17（北京时间）。使用已经通过小批次验证的**本轮新 factory 对象**和
native-wave v4 provider，计划执行 150/200 × BE_NR/BD/ROS4 × pool 8/32，
每组 32→33 单元，各 4 步／物理时长 1e-10；未改变 rho/T/cv/rtol、原误差预算或库。

首组 `audit150-be_nr-pool8` 达到原 1,800 秒墙钟限制，worker 返回 1。
**完整 harness 0/12，不是容量资格通过。** 32 单元的四个 CPU/GPU 对照步已完成，
33 单元只完成首个 CPU 参考步；后续十一组没有运行。

| 32 单元步号 | CPU 秒 | GPU 秒 | 显式 kernel 数 | 同步数 |
|---|---:|---:|---:|---:|
| 0 | 54.452551169 | 653.136127369 | 416723 | 31702 |
| 1 | 21.207984460 | 149.736871494 | 162535 | 11844 |
| 2 | 9.482825881 | 87.899545910 | 74127 | 5652 |
| 3 | 7.924894537 | 48.782858262 | 42594 | 3330 |

这些是失败运行的诊断耗时，不能用作正式加速比或完整轨迹验收。
原输入为 IdealGas 固定 cv、原 harness NSE 关闭；不是 Helm 全应用测试。

护栏记录：总观察 1812.525 秒，owned RSS 峰值 908672 KiB，Host 最低可用
113502380 KiB；swap 始终 9256 KiB。整设备显存峰值 10556 MiB、最低剩余
9207 MiB；I/O full 峰值 0.115%，memory full 0%。三类观察完整，
`guard_stopped=False`，没有 OOM／swap 增长／资源护栏中止证据。

## 保全与后续

服务器与本机两份档案均通过 SHA、全部原文件和全部投影的逐字节核验。
本次 verifier 也核验 raw 内嵌投影，并拒绝档案内未计入库存的普通文件。

- raw：13,513,415 bytes；SHA `3bbf408a9eb31b8493822a59878f4d08a12a11022a3a12737e049f2fb5698ed9`。
- compact：105,736 bytes；SHA `c2d31307a115aa1388f099434895a246c4dd76db88ebaa6c184cc6cf18f3b3bb`。
- 40 个原文件、22,900,658 原文件 bytes；35 个文本投影、5 个 raw-only 产物。
  包括本轮 150/200 的 CUDA factory 对象、provider、首组可执行文件和 Host 对象。
- 完整源码／网络及编译身份通过父级 [factory-focused-v1](../factory-focused-v1/README.zh-CN.md)
  已保全档案和本包中的 SHA manifest 关联；不声称本增量重复存储整个源码树。

新 `capacity-v2` 只将单组墙钟上限改为 7,200 秒，其余科学参数与 12 组计划不变；
源码和产物不覆盖，输出另建。配方位于 `next-wall-only-recipes/`。
截至本记录写入，新一轮仍在运行，不能提前计入完成或通过。

`tool-tests.log` 是 17 个本机合成归档／准备工具测试，不是新增 GPU 验证。
批量 kernel 试验在[独立候选目录](../../../native-wave-candidate/batched-kernels/README.zh-CN.md)，
当前仅准备，未编译、未接入生产、未宣称性能改善。
