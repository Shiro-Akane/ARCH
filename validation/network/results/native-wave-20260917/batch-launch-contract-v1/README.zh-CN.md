# 批量缩放／残差 kernel：真实合同通过

2026-09-17：128 个不同 kernel 组合逐字节对照通过，151/201 阶 × capacity 1/2/8/32
八项 provider 合同通过。全部 18 条构建／链接／测试命令 exit 0；源和产物前后身份一致。
这是执行等价性与求解器合同，不是核反应、完整 Helm、sanitizer 或性能资格。

## 实际构建范围

重新编译 Host provider、轻量 `SparseWaveKernels.cu`、两份 Host 测试入口，
重新链接两个真实 GPU 测试。私有 archive 只替换 `CuDssSparseWaveSolver.cpp.o`、
添加 `SparseWaveKernels.cu.o`，其他成员保持 SHA 一致；原共享缩放／残差对象没有改动。
保留 strict-FP、cuDSS 0.8.0.10、BTF_COLAMD、原残差和修正次数。
未重新编译巨型核反应 factory，未注册进生产 CMake，未构建完整 ARCH。

| 编译单元 | 墙钟秒 | `/usr/bin/time -v` 峰值 RSS KiB |
|---|---:|---:|
| Host provider | 1.33 | 152860 |
| 批量 CUDA kernels | 1.43 | 197372 |
| kernel 对照入口 | 1.12 | 139280 |
| provider 合同入口 | 1.25 | 162996 |

新 provider archive SHA：`0b902a1e7d87174bc395d4be328713390d36a28320353d55113678e57f697204`。
kernel-test SHA：`e5cd8e0dd5ed3bcbdafd0002d59dba308dd097f87a0f8f3a0d529dbdad102d47`。
provider-test SHA：`ca308189c9ad6d9702ea7b52ba50803e1041ab60a450dbfb4b89a03919aef31a`。

## 检查覆盖

kernel 组合为 extent 1/151/201/513 × capacity 1/2/8/32 ×
正常／大尺度差／NaN 矩阵／Inf RHS × 全部／部分 active，共 128 个不同组合。
检查缓冲逐字节一致、原输入不变、inactive 哨兵、状态及修正 RHS 两种模式。
八项 provider 合同继续覆盖缓存 token、factor-only、初始未用槽、失败恢复和非法输入。

五个新 kernel 的寄存器数依次为 36/36/28/27/38，静态 local/shared 均为 0，
最大线程数均为 1024；本候选实际 CTA 为 256。该查询不等于动态 spill／访存或性能测量。
原准备包的错误计数已在部署前修正为 128，未减少 C++ 测试组合，旧准备失败日志保留。

guard elapsed 14.072 秒，Host 最低可用 114,179,740 KiB，采样 RSS 峰值 314,488 KiB；
swap 9,256 KiB 不增长，未触发护栏。采样的整设备显存峰值 539 MiB，最低余量 19,225 MiB，
I/O full 最大 0.331%。采样高水位可能低于短时峰值，不替代逐进程 `/usr/bin/time` 统计。

## 双端恢复及下一步

- raw：1,285,581 bytes，SHA `9caea00ea1dda8c67adae4ba8db4e8878da9143300b8a08469f452e1e8bfcf75`。
- compact：98,294 bytes，SHA `98044c7a42c4aa635a7d7318cb8650fc531b75ff477a99ca42c3b71b29900460`。
- 服务器：`/home/ubuntu/projects/ARCH-native-wave-v4-20260916/batch-launch-{raw,compact}-v1.tar.zst`。
- 本机：`C:/tmp/ARCH-perf-20260909/build/native-wave-batch-contract-v1-download/`。

全部 92 个原文件／3,393,681 bytes、84 个原字节投影、raw 内嵌投影及两包完整库存核验通过。
8 个对象／二进制仅在 raw 保存。见 [收集回执](batch-launch-collection-v1.json)、
[本机回执](batch-launch-local-receipt-v1.json) 与 [原清单](raw-manifest.json)。
仅在双端核验后启动真实 150/200 × 三 ODE 的 focused 燃烧轨迹；结果待收齐。
