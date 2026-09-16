# Native wave：新 factory 构建与小规模三 ODE 通过

2026-09-17（北京时间）。这是隔离候选的真实 GPU 数值门槛，不是完整 ARCH、长轨迹、NSE 或性能验收。生产源文件尚未切换到该候选，外部 KLU/cuDSS 未修改。

## 实际执行的范围

在独立源码树 `ARCH-native-wave-v4-20260916/source`（基准提交 `8da9b23d` 加已通过独立合同的 v4 overlay）和全新 `factory-release` 中，串行重新编译并链接 audit150、audit200 的 typed factory 测试。没有用旧 device factory 对象替代新调度器。新 factory 对象明确引用 `CuDssSparseWaveSolver`。

- 两个实际可执行文件和 `libarch_cuda_sparse_provider.a` 均生成，source 474 项、network 50 项、vendor 3 项及产物 SHA 在前后核验。
- audit150 重型 CUDA TU：3,004.34 秒，peak RSS 2,714,148 KiB；audit200：6,163.00 秒，4,003,944 KiB，均 exit 0。
- 整个串行构建 guard：9,205.855 秒，peak owned RSS 4,028,996 KiB；未触发 guard，没有 swap 增长。这不是 clean Debug／16 GiB 构建资格，也没有构建完整 ARCH。
- 首次安装因 canonical LF 与已验证 payload 原字节的 CRLF 差异被拒绝，发生在修改源码／编译之前。首失败保留；第二次同时验证 pinned Git 原文、换行归一后的相同内容及 payload 原字节 SHA，再安装已测试的 payload，没有更换数学。

随后执行 CPU KLU 对照 CUDA native wave：150/200 × BE_NR/BD/ROS4 × 2→3 单元存储，每条轨迹 4 步，共 **12 条存储轨迹、48 对 CPU/GPU 宏步**。每个单元密度依原测试为 `rho0*(1+0.05*cell)`，不是所有单元相同输入。

原参数：`rho0=1e7`、`T=3e9`、物理时长 `1e-10`、IdealGas 固定 `cv=1e8`、`rtol=1e-7`、pool 2、初始 `c12=o16=0.5`；没有改步数、核素、ODE、残差或接受预算。此测试显式关闭 NSE，与原门槛一致，不外推为 NSE 验收。

## 数值结果

场误差定义为 `abs(CPU-GPU)/max(1,abs(CPU))`，不是所有字段的纯相对误差；原场预算 `2e-10`、limiter 预算 `2e-8`。

| 网络 | ODE | 最大场误差 | 最大 limiter 误差 |
|---|---|---:|---:|
| 150 | BE_NR | 6.05697e-15 | 6.19367e-15 |
| 150 | BD | 1.79550e-14 | 1.04394e-14 |
| 150 | ROS4 | 8.51751e-15 | 6.40857e-15 |
| 200 | BE_NR | 5.31471e-15 | 5.46561e-15 |
| 200 | BD | 1.85150e-14 | 1.84667e-14 |
| 200 | ROS4 | 1.19098e-14 | 1.18484e-14 |

每个网络都出现唯一完整 parity 标记，通过了全部三 ODE／两种存储／四步及演化检查，之后 source/network/vendor/product 再核验成功。六组均有实际组分演化，约 `2.57e-5`。CPU/GPU 共用反应率，因此这些对照不是独立核反应率 oracle。

实际日志与机器、driver、CUDA、compiler 信息位于 `records/ARCH-native-wave-v4-20260916/focused-control-v1/`；两份完整报告在对应 `focused-v1/audit150/`、`audit200/`。

150 guard 为 396.193 秒，GPU 全设备峰值 10,829 MiB、最低余量 8,934 MiB；200 为 556.311 秒，峰值 14,526 MiB、最低余量 5,237 MiB。两者均 complete、未触发 guard、swap 9,256 KiB 不增长。显存是整设备观测，不是 provider 私有分配量。

## 性能边界

本次逐步计时只是诊断，不是配对五次的正式加速比。150/200 的 BE GPU 累计分别为 365.666／516.579 秒，不能与串行 Host harness 的 CPU 时间相除冒充 CPU8 加速比。

与历史原 BE 小规模诊断相比，显式 kernel 数仍分别为 112,012／104,712，同步次数由 46,987／43,930 降至 32,832／30,687。历史样本不是本次交替重采样，不能由此宣称端到端显著提速；特别是“同步减少”不等于大网络性能问题已解决。下一步仍要验证 32→33 单元、pool 8/32、长轨迹，以及原生批处理的额外矩阵工作，再判断完整 Helm 应用收益。

vGPU 上 compute-sanitizer 的既有限制仍未解除，本轮没有新增 sanitizer 通过资格。

## 双端恢复与原字节

- raw：33,603,707 bytes，SHA-256 `59d5a1649d79b3ef483c84e512747f5b057cfc8e4d5c6070604d3bbe758f1f56`。
- compact：2,464,308 bytes，SHA-256 `b2e5005caa88c67dd3e51aaf8ed398f04b08c1b03ee4f9d5a99ccbb4d7f0fff0`。
- 服务器：`/home/ubuntu/projects/ARCH-native-wave-v4-20260916/factory-focused-{raw,compact}-v1.tar.zst`。
- 本机：`C:/tmp/ARCH-perf-20260909/build/native-wave-factory-focused-v1-download/`。

本机重新核验两个 archive SHA，以及 raw 清单全部 **637 文件／99,951,698 bytes** 和 compact 的 **628 原字节投影**。9 个仅 raw 文件（实际二进制、静态库、Helm 表）明确列在 `raw-only-files.json`；3 个外部动态库只记录路径、大小和 SHA，不复制或改库。原始换行没有归一化。

[双端回执](factory-focused-local-receipt-v1.json)、[原文件清单](raw-manifest.json)、[收集回执](factory-focused-collection-v1.json) 可用于恢复和复核。新增归档工具的 7 项合成测试只验证文件／失败记录合同，不增加 GPU 或物理通过数量。
