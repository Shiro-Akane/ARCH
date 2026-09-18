# 窗口 factory：容量回归 12/12 通过

2026-09-18 北京时间。150/200 × BE_NR/BD/ROS4 × pool 8/32 全部通过，
共 24 条存储轨迹、96 对 CPU/GPU 宏步。worker/runtime exit 均为 0，无超时或资源护栏中止。
这是同一新 factory 的容量正确性检查，不是多页窗口、长轨迹或完整 Helm 性能验收。

## 冻结输入与数值

每组依次使用 32、33 个非均匀单元，四步到 1e-10；rho=1e7、T=3e9、cv=1e8、
rtol=1e-7、初始 C12/O16 各 0.5。IdealGas、NSE=false，原 KLU CPU 参考与 cuDSS GPU 路径。
selected window=32，实际 owner/native capacity 分别为 8 或 32；没有跨 native page。
原 32 MiB ODE workspace、256 MiB provider 估计预算、strict FP 和逐组 7200 秒 wall 均未改。
此前 focused 全部通过、双端保全和 GPU 空闲后才启动本轮，未重编 factory 或修改库。

输入包 51,200 bytes，SHA `116611b7470ea7e8717f349edbfcbf8cff5614fac45e10f9368b8224f4bbec30`。
两份新 factory 与源/网络/依赖身份继承并核验[上轮 focused](../window-focused-v1/README.zh-CN.md)。
原 validator、harness 和所有逐步数值门槛不变：字段 `abs(CPU-GPU)/max(1,abs(CPU)) <= 2e-10`，
limiter `<= 2e-8`，保留组分非负、闭合及实际演化要求。

两种 pool 的数值误差相同：

| 网络／ODE | 最大场误差 | 最大 limiter 误差 |
|---|---:|---:|
| 150／BE_NR | 8.55694e-15 | 4.89505e-15 |
| 150／BD | 2.78910e-14 | 1.30956e-14 |
| 150／ROS4 | 1.18601e-14 | 5.78624e-15 |
| 200／BE_NR | 7.84589e-15 | 5.37632e-15 |
| 200／BD | 3.19898e-14 | 2.11537e-14 |
| 200／ROS4 | 1.19098e-14 | 6.99570e-15 |

最大组分变化分别约 6.68775e-5、6.68984e-5，不是空燃烧测试。

## 单次诊断成本，不作正式加速比

下表为每个 harness 八个宏步时间之和，保留慢结果。CPU 是逐单元参考，不是全应用 CPU8；
没有五次交替重采样，不能用于宣称新版加速、统计显著性或完整应用表现。

| 网络／ODE | CPU 秒（pool 8／32） | GPU 秒（pool 8／32） |
|---|---:|---:|
| 150／BE_NR | 188.886582／186.544352 | 1974.499267／843.367905 |
| 150／BD | 1.764380／1.662390 | 9.992530／3.912212 |
| 150／ROS4 | 5.683737／5.476088 | 33.102376／13.284823 |
| 200／BE_NR | 266.080428／274.149759 | 2294.272274／1121.952218 |
| 200／BD | 2.389380／2.410877 | 13.827565／5.285390 |
| 200／ROS4 | 7.354965／8.666357 | 47.116817／18.671664 |

pool32 下 GPU 步时间仍约为 CPU 参考的 2.15–4.52 倍。该剩余瓶颈属于当前超大网络路线，
不能外推为流体、AMR、扩散或 aprox13 内置燃烧也慢。

## 资源、身份与双端保全

护栏 elapsed=7341.895 秒（约 2 小时 2 分），min available=115,324,772 KiB，
peak owned RSS=467,284 KiB；swap 基线与峰值均为 67,988 KiB。
GPU 整设备 peak=14,668 MiB、min free=5,095 MiB，观察完整；无护栏中止。
这些不是 provider 独占显存或新 sanitizer 资格。
运行前后源码、网络、库、factory 和配方校验全部通过。

- raw：21,832,851 bytes，SHA `e4e37859fba9163b9fc25049f2f13e912d6d662e9649b831e51db40ec3fc3928`。
- compact：148,036 bytes，SHA `8f54946a88fd8e9798d14a67b05b61d9fef2c77215b8bc1b1fe70a1acf025b9b`。
- 128 个原文件共 35,481,533 bytes、78 个原字节投影及两包/内嵌清单已核验；50 个二进制、对象或不变源码成员仅在 raw。
- 服务器前缀：`/home/ubuntu/projects/ARCH-native-wave-v4-20260916/window-capacity-`。
- 本机原包：`C:/tmp/ARCH-perf-20260909/build/window-capacity-v1-download/`。

见[收集回执](window-capacity-collection-v1.json)、[本机回执](window-capacity-local-receipt-v1.json)
和[实际运行记录](records/ARCH-native-wave-v4-20260916/window-capacity-v1/record.json)。
本机回执已传回服务器；提交前再次核验两包、128 个原成员、78 个 Git 投影，并用原容量 validator 复核十二组真实日志。
发布前另重跑 43 项窗口工具回归，全部通过；日志见 `publication-tooling-tests.log`。
这些是工具/协议测试，不增加 GPU 运行数或物理资格。

## 阶段交付决定

用户于 2026-09-18 决定优先推送其他模块已验证的阶段成果，150/200 的速度优化后置。
本轮结果一并保存，不启动后续大网络长轨迹或 64/128 窗口实验。
window/native-wave/leaf-inline 仍为 validation-only；没有替换生产 provider 或宣称性能达标。
将来恢复时仍需长轨迹、真正多页窗口、真实 Helm 全应用及正式配对计时，不能拿本轮容量通过替代。
