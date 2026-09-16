# 原 BE 长程补测：四组数值通过，性能未验收

2026-09-17（北京时间）。本次只延长整次运行 wall 上限至 21600 秒，不改变物理时长、单元数、核素、ODE、容差、CPU KLU、CUDA factor-cache v2 或共享数学。原 1800 秒超时证据仍保留。用户暂停本机期间，原服务器 worker 连续运行；恢复后只接续收集，没有重启或拼接轨迹。

audit150／audit200 × pool 8／32 四个 harness 全部通过，每个均完成非均匀 storage 32→33、各 16 步至 1e-9：共八条完整 storage 轨迹、128 对 CPU/GPU 宏步。rho=1e7、T=3e9、cv=1e8、ODE rtol=1e-7；原场差 2e-10 和 limiter 2e-8 门槛未改，没有观察器插桩。

| 网络 | 池容量 | 最大场差 | 最大 limiter 差 | CPU 尝试／拒绝 | GPU kernel／同步次数 |
|---|---:|---:|---:|---:|---:|
| audit150 | 8 | 1.054069e-14 | 7.186728e-15 | 318193／4351 | 3387854／1374023 |
| audit150 | 32 | 1.054069e-14 | 7.186728e-15 | 318193／4351 | 3269754／1315125 |
| audit200 | 8 | 9.868707e-15 | 5.889650e-15 | 298476／4339 | 3178987／1289341 |
| audit200 | 32 | 9.868707e-15 | 5.889650e-15 | 298476／4339 | 3068159／1234047 |

最大组分演化约 6.698012e-4／6.701681e-4，不是无演化空跑。相同误差最大值不等于逐位一致，也不证明 GPU 内部 ODE 尝试／拒绝次数与 CPU 相同。

仅作诊断的 GPU 步耗时合计：150/pool8 5226.443499 秒、150/pool32 2726.735402 秒、200/pool8 6226.136905 秒、200/pool32 3610.148901 秒。这是每种配置一次、CPU/GPU 交替执行的正确性 harness，不是正式重复计时或 CPU8 加速比。完整 ARCH＋真实 Helm 应用此前仍比 CPU8 慢约 5.0–10.3 倍；本结果不改变该性能结论。

资源护栏未触发。总 wall 19991.672 秒，所属进程峰值 RSS 1360712 KiB；最小 Host available 113118388 KiB，swap 9256 KiB 且无增长。whole-device 峰值显存 16315 MiB，最低空闲 3448 MiB，18872 次完整观测；I/O full 峰值 0.504%，低于原 50%／10 秒护栏。这不是“256 MiB 总显存”的证明，后者只是缓存附加预算口径。

## 证据与边界

[完整性审计](integrity-audit.json) 核对原源码、配方、provider、factory、可执行文件身份，全部 38 个 raw 文件和 31 个文本投影的 SHA，以及原八个编译／链接／运行命令、四组完成标记、逐步记录和资源护栏。审计工具的八项合成测试只验证工具，不增加 GPU 科学资格。

- raw：`large-be-extended-wall-v1.tar.zst`，21154280 bytes，SHA-256 `fbebcb08b34f4876f6625ea2c27944fb77c41e3eb3de9a753e239d7ff38104c7`。
- compact：`large-be-extended-wall-compact-v1.tar.zst`，39748 bytes，SHA-256 `5d8109bb24aeb02aac866d669f5392ab0b25b85c91d6510c1efc164533b6cc00`。
- 两端位置：服务器 `/home/ubuntu/projects/ARCH-microphysics-20260914/build/`，本机 `C:/tmp/ARCH-perf-20260909/build/`；已分别核对大小和 SHA。
- `records/` 原字节保留；ELF／对象／archive 不进文本投影，仍完整留在两端 raw。`controller.log` 与 `collection.log` 是运行结束后的额外收集回执。
- 这是 constant-cv IdealGas 的原非均匀长轨迹，不替代真实 Helm 全应用、独立反应物理 oracle 或 sanitizer。vGPU 的 sanitizer 限制仍存在。

下一步是在新目录真实编译并验证隔离 native-wave provider，再用全新 factory 对三 ODE／150、200 核素进行验证，最后进入完整应用及配对性能比较。不会用旧 factory 冒充新调度资格。
