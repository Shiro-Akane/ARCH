# 150/200 核素：容量缓存的补充长轨迹证据

这些是集中式数值/容量诊断，**不是完整 ARCH 应用或 CPU8 加速比验收**。生产 provider 保持 V1 原系统残差门槛、有界修正和 256 MiB 可选 factor-cache 预算；未改库、网络、ODE 或精度预算。

| 组别 | 规模与时长 | 结果 |
|---|---|---|
| BE 原门槛重验 | 150/200；2→3 单元；池 2；4 步，1e-10 | 2/2 通过 |
| BD/ROS4 长轨迹 | 150/200 × 两 ODE × 池 8/32；32→33 单元；16 步，1e-9 | 8/8 通过；最大场误差 3.2154e-14 |
| BE 小规模长轨迹 | 150/200；2→3 单元；池 2；16 步，1e-9 | 2/2 通过；最大场误差 9.8688e-15 |
| BE 大规模长轨迹 | 150；32→33 单元；池 8；16 步，1e-9 | **未通过**：原 1800 秒超时，未完成第一种存储规模；原始失败保留 |

场比较采用 `abs(CPU-GPU)/max(1,abs(CPU))`，不是所有字段的相对误差；原预算 2e-10。limiter 原相对预算 2e-8。`summarize.py` 可从原记录重新核对通过数和误差，失败样本不进入通过统计。

测试内单元密度为 `rho0*(1+0.05*cell)`，因此 2/3 与 32/33 单元还存在密度/刚性范围差异；不能用小规模 BE 通过替代大规模超时。此处使用 IdealGas 固定 cv 参考，完整应用另测真实 Helmholtz EOS。CPU/GPU 共用反应率，因此也不是独立核反应率 oracle。

大规模 BE 首个宏步的诊断计时为 CPU 81.30 秒、GPU 1177.87 秒；同时存在 CPU 编译，不能据此报告正式加速比。ARCH/provider 的显式 kernel/sync 计数不包括 cuDSS 内部全部 kernel。超时资源记录未显示 OOM，不能把时间失败归为显存不足。

## 原始证据与恢复

`records/` 保留四组 JSON（包括失败）和资源记录；`provenance/` 保留实际编译/链接配置及原 factory 对象指纹。完整原始 stdout/stderr、重链接可执行文件、provider 和 overlay 在以下归档中，两份完整 SHA 已验证：

- 服务器 `/home/ubuntu/projects/ARCH-microphysics-20260914/build/large-focused-followup-v1.tar.zst`；本地 `C:/tmp/ARCH-perf-20260909/build/large-focused-followup-v1.tar.zst`。
- 大小 57,293,287 bytes，SHA-256 `191b8aa55b8fd5e40dcda06189b19ece2c3600b0bee2a6d3426fb27d648bef1d`。
- compact 同目录 `large-focused-followup-compact-v1.tar.zst`，85,228 bytes，SHA-256 `731bbc177929e96489795767c481e6e0dbf87da547a030451c958002952ec6e8`。

factory 数学来自原记录绑定的旧对象，仅重编 Host harness 并替换有记录的 provider；这不冒充完整应用重编通过。完整 clean large ARCH 构建与全应用互操作/计时仍单独验收。
