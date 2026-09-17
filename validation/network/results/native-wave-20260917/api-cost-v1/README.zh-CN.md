# 六组大网络 API 成本诊断完成；下一步检查网络数学执行成本

2026-09-17：150/200 核素 × BE_NR/BD/ROS4 共六组全部完成，原四步至 1e-10、
2→3 单元、pool2、rho/T/cv/rtol、C12/O16 和科学预算均未改变。
12 条存储轨迹、48 对宏步通过原场 2e-10／limiter 2e-8 检查，且组分确实演化。
沿用已独立复核的同一对可执行文件，只加入两个 Host API 观察器；没有重编 CUDA factory。

## 观测结果

单位均为秒。第二列是 harness 内八个 GPU 宏步区间之和；后三列是累积 Host API 延迟。
**这些区间可能嵌套，不能相加、当作精确 GPU kernel 耗时或据此计算加速上限。**
等待标签只说明最近观察到的 ARCH launch，不追踪所有 cuDSS 内部 kernel，也不证明唯一因果。

| 网络／ODE | GPU 宏步诊断区间 | advance 后 D2H Host 等待 | cuDSS factor Host | cuDSS solve Host |
|---|---:|---:|---:|---:|
| 150／BE_NR | 363.662142 | 339.640933 | 13.681195 | 8.732609 |
| 150／BD | 3.077151 | 2.162585 | 0.103612 | 0.561214 |
| 150／ROS4 | 8.701143 | 7.492308 | 0.263951 | 0.761419 |
| 200／BE_NR | 514.412222 | 484.109807 | 17.059023 | 11.674652 |
| 200／BD | 3.048761 | 2.236138 | 0.099859 | 0.551868 |
| 200／ROS4 | 12.335127 | 10.891736 | 0.341206 | 0.902423 |

所有组的原 scientific metrics 与未插桩 focused 一致；最大场差 1.8515005e-14、
limiter 差 1.8466661e-14，最大组分变化约 2.57e-5。没有 CPU 回退或缩短燃烧时间。
每组 analysis 一次；factor/solve 实际调用分别为 10936/10945、78/709、207/877、
10224/10224、58/510、204/816。保留所有调用，不能把整个 cohort 的 factor 算成只处理一个有效 lane。

目前更值得检查 ODE 内的网络执行成本，而不是未经测量更换稀疏库。
下一项隔离试验只把 generated JacobianSink 的小型 set 适配器从禁止内联改为内联，
不改生成的反应公式、矩阵内容、网络、求解器、精度或容差。
必须先通过原 CPU/GPU 叶函数及原版/候选完整向量对照，测得收益后才考虑 ODE 集成。

## 资源与资格边界

worker exit0；guard 942.857 秒，采样 Host RSS 峰值 387,788 KiB、最低可用
114,112,120 KiB；swap 9,256 KiB 不增长；整设备 GPU 峰值 14,520 MiB、最低余量
5,243 MiB。memory full 最大 0%，I/O full 最大 0.113%，护栏没有触发。
source/network/vendor/artifacts/recipe 前后身份检查完整通过。

`diagnostic_numerical_pass=true`；未插桩 `trajectory_matrix_pass=false`，
正式性能、完整应用和 release 资格仍为 false。
这不是独立反应率参考、Helm、长期、sanitizer 或完整应用性能验收。
本机工具检查与真实 GPU 运行数分开，观察器的重复快照不算新样本。

## 双端保全

- raw：7,748,210 bytes，SHA `ab56c01d6d5eb1048220c943c403c1bbc61c89cc6cd783e6428030f2aebd123e`。
- compact：76,894 bytes，SHA `fc88b2b1df78a9882401bc119a4cc824dc10800a52c26f7886e8b7d7a4ced574`。
- 服务器前缀：`/home/ubuntu/projects/ARCH-native-wave-v4-20260916/api-cost-`。
- 本机目录：`C:/tmp/ARCH-perf-20260909/build/api-cost-v1-download/`。

57 个原文件／14,113,273 bytes、53 个原字节投影、raw 内嵌投影及完整库存校验通过；
4 个二进制仅在 raw 保存。见[收集回执](api-cost-collection-v1.json)、
[本机逐字节回执](api-cost-local-receipt-v1.json)。保全后才启动不同目录的叶函数对照。
