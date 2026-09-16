# ROS4／RKL1 首轮 I/O 护栏中止：完整保全后从头重试

这是一份失败保全与接续记录，不是第十组正式验收通过报告。
前九组的 972 次运行／945 次比较仍然有效；本次首轮数据不计入该已通过总数。

## 实际发生的情况

首轮 `coupled_ros4_rkl1_all_transport` 在 80 次运行完成后被系统资源护栏中止。
原 `evidence.json` 留有 81 条 lane：80 条已完成、最后一条仍为 `running`；
77 次先前比较已记录。最后一条是 128 块、正式 repeat 0、候选 CPU1。
原 worker PID 4125711 返回 125，本地原控制器随即退出，后续 ROS4／RKL2 和 BE 长轨迹未启动。

原始护栏给出的停止原因为 `io_pressure`：系统 I/O full pressure 峰值 71.733%，
触发原 50%／持续 10 秒规则。内存 full pressure 为 0%，最低 Host available 为
112,628,132 KiB，swap 基线和峰值均为 9,256 KiB；不是内存不足触发的终止。
护栏共运行 5,168.720 秒，peak_owned_rss=1,885,764 KiB。
GPU 观测 peak_used=2,975 MiB、min_free=16,788 MiB；4,861 条观测被明确标记为不完整。

系统级 PSI 本身不能定位到某个后台进程或宿主存储服务，不据此归责某个服务。
没有终止他人的 Python、TdxW、Wine 或桌面服务，也没有降低任何护栏。
终止后检查未发现仍在运行的 ARCH／NVCC／ptxas／cc1plus 或 GPU 计算作业。

## 保全与独立重试

1. 原始失败报告保持字节不变，未补写成功状态或 `identities_after`。
   在单独的 `failure-identity.json` 中记录故障后源码、构建及运行环境身份，并与原始 before 身份核对一致。
2. 保存完整 raw 和 compact，各自在服务器与本机核对 SHA-256。
   [本地复核](local-backup-verification.json) 验证 878 个 raw 清单成员、541 个文本投影，
   以及 337 个仅保留于完整 raw 的 HDF／大 TSV 项；raw 包中确有全部清单成员。
3. 在双备份之后，把原 run 目录、九个前缀文件和 controller 目录共 11 项迁至独立 `preserved/`。
   [迁移账本](relocation.json) 逐文件校验原字节，`pending_move=null`；没有删除 HDF 或原日志。
   controller 目录最后迁移，避免在旧 run 仍未保全时释放原控制器的命名空间。
4. 再次复核前九组，并进行额外的 I/O 安静检查。2026-09-16 01:45:04 UTC 的
   [启动前记录](pre-retry-quiescence.log) 显示 I/O full avg10=0.00、avg60=0.00、avg300=0.06，
   可用磁盘 8,865,107,968 bytes，高于原 8 GiB 启动门槛。
   额外的 avg10／avg60 ≤1% 检查只用于重试入口，运行中的原 50%／10 秒护栏未变。
5. 启动独立的新完整采样，worker PID 4150866。原冻结 v3 配方的
   SHA-256 仍为 `bb8d31abf2c83bb8007f563665354e292ff26219bf81938e9145be1923df2479`。
   重跑全部 8／32／128 块、一次预热及五次正式重复，不复用首轮的任何计时或比较样本。
   两组 ROS4 完成并双备份后，才继续原 BE 延长 wall 长轨迹队列。

这里复用了原配方的工作路径，但旧路径内容已经完整保存在独立目录和两个 raw 副本中。
新尝试没有覆盖旧证据，也没有将不同尝试拼接为一组成功结果。
物理输入、网络、EOS、ODE、终止时间、精度、输出和样本数均未改变。

## 文件与位置

- [完整失败记录与原始报告](evidence/failure-record.json)、[原报告](evidence/evidence.json)。
- [原始清单](evidence/raw-manifest.json)、[文本投影映射](evidence/projection-map.json)、[省略项索引](evidence/omissions.json)。
- [归档身份](archive.json)、[九组复核](pre-retry-nine-module-audit.json)。
- [保全配方](preserve_failed_formal.py)、[本地复核器](verify_local_backup.py)、[接续配方](resume-after-io-pressure.ps1)。
- [20 项本地机械测试](local-mechanical-tests.log) 通过，涵盖非目标失败拒绝、换行字节、路径边界、
  覆盖拒绝和中途迁移失败账本；它们不是 CUDA、物理或 sanitizer 测试。

raw：`failed-ros4-rkl1-io-pressure-20260916-v1.tar.zst`，18,076,049 bytes，
SHA-256 `19f502879195af86717968734de9701b1fbba74d79b6683fb0b49a61f006df87`。
compact：612,944 bytes，SHA-256 `d4bdd1283687574d9a26d41ff7b397cc8fd083ec90e02f160efed431904a0fa0`。
两包保留于服务器 `ARCH-multiphysics-fix-20260914/build/` 和本机
`C:/tmp/ARCH-perf-20260909/build/`。完整原目录还在服务器
`build/fix-20260914/timing/failed-ros4-rkl1-io-pressure-20260916-v1/preserved/` 中。

本记录不提供第十组加速比，不标记燃烧／扩散总任务完成，也不改变尚未验证的 native-wave 候选状态。
