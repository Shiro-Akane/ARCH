# 批量 kernel focused：实际运行完成，首版汇总解析失败

2026-09-17：150/200 × BE_NR/BD/ROS4、2→3 单元、pool2、四步至 1e-10 的
六个实际 harness 全部 exit 0 并打印完整 parity 标记。外层 runner 随后在日志解析处 exit 1；
本目录保留该失败和原 `qualification.json` 的 false，不改写为成功。

原因：冻结 C++ `test_generated_sparse_burn.cpp` 为保持历史输出格式，在默认
`storage={2,3}, pool=2` 时不打印 `storage_controls`。新增汇总器和最初合成 fixture
错误地要求该行必定存在。没有数值预算失败、CUDA 错误、超时或资源护栏触发。
后续布局试验因此被自动阻止，直到[独立只读复核](../batch-launch-focused-reaudit-v1/README.zh-CN.md)
完成且双端保全；未重写任何原始日志，未为了验证器更改物理程序。

## 原始运行身份及结果

仅重新编译两个 Host harness，复用本轮 fresh factory 对象，链接通过合同的私有 provider。
实际 150 executable SHA `166f71a0c942f4763fa055e683669a1823b4cd8ae9512a723271edb5e0b60181`；
200 SHA `24dbea4bb82186d04b6e570a6a4187b2851e2d7c49a4f36fcfeb0c2f9bb758d2`。
provider SHA `0b902a1e7d87174bc395d4be328713390d36a28320353d55113678e57f697204`。
IdealGas/NSE=false，rho1e7、T3e9、cv1e8、rtol1e-7、C12/O16 各0.5；
场预算2e-10、limiter预算2e-8。不是 Helm 应用或独立反应率 oracle。

| 网络／ODE | 最大场误差 | 最大 limiter 误差 | GPU 诊断秒 | 显式 kernel／同步数 |
|---|---:|---:|---:|---:|
| 150／BE_NR | 6.05697e-15 | 6.19367e-15 | 361.543369 | 76632／32832 |
| 150／BD | 1.79550e-14 | 1.04394e-14 | 3.024348 | 3862／1546 |
| 150／ROS4 | 8.51751e-15 | 6.40857e-15 | 8.689844 | 5222／2134 |
| 200／BE_NR | 5.31471e-15 | 5.46561e-15 | 514.351193 | 71612／30687 |
| 200／BD | 1.85150e-14 | 1.84667e-14 | 3.053362 | 2826／1151 |
| 200／ROS4 | 1.19098e-14 | 1.18484e-14 | 12.331216 | 4940／2055 |

实际最大组分变化约2.57e-5。数值与前一 v4 focused 结果一致，kernel 启动数减少，
但这组小规模单次诊断没有显示足以解决大网络性能差距的改善。
没有重复交替正式样本，不能把这些秒数或历史差额当成 CPU8 应用加速比。

guard965.240秒，Host最低可用113,094,732KiB、采样自有RSS峰值1,390,320KiB；
swap9256KiB不增长，未触发护栏。整设备GPU峰值14522MiB、最低余量5241MiB，
最大I/O full0.200%，三个观察完整。source/network/vendor/artifacts及配方 after checks 完成。

## 原失败双端保全

- raw：21,574,449 bytes，SHA `058b3d74cbb0cf51078eac1ed0a790dac9796c8b3e134609d66d00a79820987f`。
- compact：122,251 bytes，SHA `40a7569283e06382453a20d42eb6c7bf145c2e5ad040246f17da1bdecdded4c5`。
- 服务器前缀：`/home/ubuntu/projects/ARCH-native-wave-v4-20260916/batch-launch-focused-`。
- 本机：`C:/tmp/ARCH-perf-20260909/build/batch-launch-focused-v1-download/`。

65个原文件／35,709,256 bytes、58个原字节投影、raw内嵌投影及完整库存均核验。
7个二进制／对象保存在raw。见[失败收集回执](batch-launch-focused-collection-v1.json)
与[本机回执](batch-launch-focused-local-receipt-v1.json)。
