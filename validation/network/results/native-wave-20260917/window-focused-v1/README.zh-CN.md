# 窗口 factory：六组真实燃烧小轨迹通过

2026-09-18 北京时间（服务器 2026-09-17 UTC）。6/6 harness、12 条存储轨迹、
48 对 CPU/GPU 宏步通过。worker exit 0，完整日志再次独立复核，服务器／本机双包逐字节保全。
这是 IdealGas、NSE=false 的小规模接入检查，**不是多页 ODE、Helm 全应用或性能验收**。

## 原协议与结果

使用原 frozen focused 输入 v2，两份已全新编译并归档的 window factory，无重编或运行中改动。
150/200 × BE_NR/BD/ROS4，各使用 2→3 非均匀单元、pool=2、四步至 1e-10；
rho=1e7、T=3e9、cv=1e8、rtol=1e-7、初始 C12/O16 各 0.5。
字段预算 `abs(CPU-GPU)/max(1,abs(CPU)) <= 2e-10`，limiter 预算 `2e-8`，
组分非负／闭合和真实燃烧演化检查仍由同一 C++ harness 执行，没有改判据或科学参数。

| 网络／ODE | 最大场误差 | 最大 limiter 误差 | CPU 步时间总和（秒） | GPU 步时间总和（秒） |
|---|---:|---:|---:|---:|
| 150／BE_NR | 6.05697e-15 | 6.19367e-15 | 12.786359 | 364.403931 |
| 150／BD | 1.79550e-14 | 1.04394e-14 | 0.119919 | 3.026517 |
| 150／ROS4 | 8.51751e-15 | 6.40857e-15 | 0.342240 | 8.675797 |
| 200／BE_NR | 5.31471e-15 | 5.46561e-15 | 17.476585 | 513.906118 |
| 200／BD | 1.85150e-14 | 1.84667e-14 | 0.144152 | 3.287879 |
| 200／ROS4 | 1.19098e-14 | 1.18484e-14 | 0.553469 | 12.217343 |

时间来自每份原 stdout 的八个宏步之和，仅为单次 harness 诊断。
CPU 参考是逐单元求解，不是完整应用 CPU8；没有重复配对采样，不能当正式加速比。
本小输入的 GPU 仍明显慢，不能隐去。selected window=32，但实际 capacity/native 均只有 2；
本轮不检验更大并发的速度，更不能证明 64/128 窗口或跨 block 聚合有效。
两网络最大实际组分变化分别约 2.57343e-5、2.57418e-5，非空燃烧测试。

## 身份、资源与归档

- audit150 二进制 SHA：`0b2ba61e835d050db3ab057a36cb8dc37ee2f70c76e0b5a61f7f25a8b61810a5`。
- audit200 二进制 SHA：`584d82d8aeaef8df61e292f68cbb8a3e9ad27636ad588c3a0bdf150f18e173dc`。
- 原输入 tar SHA：`4e05dc6d34a4d896144ce55f1349a77a1c1a7e6ff0835415c3b23e16d852eea3`。
- C++ harness SHA：`551d021ac6ce26378dff6823e135a499cc9bbdc76276a7612a45ef21b5cab205`。
- 原 validator SHA：`1e0806d57fc539c72afe3ebd88e3ebd9b1181e8386c48d45442ff168c09173f6`。

474 个源码、50 个网络文件、3 个 vendor 文件、基线产物和 7 个新 factory 产物均保持身份。
原完整 private source/dependency inventory 也在运行前后重新核验。
独立启动前快照见 `window-ready-preflight-v3.json`：GPU 无计算进程、显存 0 MiB。
没有停止其他项目、修改库或提高预算。

原资源护栏 elapsed=943.520 秒，min available=115,370,656 KiB，
peak owned RSS=467,780 KiB，swap 基线及峰值均 67,988 KiB；护栏未中止。
GPU 整设备峰值 14,520 MiB，最低余量 5,243 MiB；观察完整。
这些不能当作 cuDSS 因子的独占内存或 16 GiB 主机验收；vGPU sanitizer 限制仍在。

raw 21,772,408 bytes，SHA `5bda80e27a523901a2dd35d59a5b65d0e55bdd9ec66b0839c92c63ec1238613b`；
compact 141,069 bytes，SHA `52e051d57ed60b87b0a1be3a3ba2bfe76606cd213a35eb674565eba76e451f74`。
112 个原文件共 35,391,754 bytes、62 个原字节投影、raw 内嵌投影及两包完整库存均已核验；
50 个二进制/对象或不变源码成员只在 raw 中，未伪称全部进入普通 Git 文本投影。

服务器前缀 `/home/ubuntu/projects/ARCH-native-wave-v4-20260916/window-focused-`；
本机原包在 `C:/tmp/ARCH-perf-20260909/build/window-focused-v1-download/`。
见 [收集回执](window-focused-collection-v1.json)、[本机回执](window-focused-local-receipt-v1.json)
及 [实际运行记录](records/ARCH-native-wave-v4-20260916/window-focused-v1/record.json)。
本机回执已传回服务器。下一步为同一 factory 的 32→33 单元、pool 8/32 容量回归，
随后才是长轨迹、真正多页窗口和真实 Helm 全应用／性能，不跳过这些门槛。
