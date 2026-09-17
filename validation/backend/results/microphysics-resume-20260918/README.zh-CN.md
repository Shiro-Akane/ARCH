# 燃烧／扩散恢复检查

2026-09-18 北京时间（原始机器日志为 2026-09-17 UTC）。本次只有 CPU 合同补测、
只读构建身份检查和工具回归，**没有新的 GPU 数值或性能验收**。

## 结果

- 合并主线 CPU CTest **9/9 通过**：`sparse_ode_continuation`、`sparse_residual`、
  `burn_mainline_reference`、`generated_nse`、`helm_components`、`mainline_authority`、
  `shared_stage_scheduler`、`amr_flux_surface_plan`、`compensated_sum`。
- 本机窗口配方/协议 **34/34**，新增只读恢复检查 **5/5**；这些包含模拟 GPU 忙闲状态，
  不是实际 GPU 测试。通过只说明检查逻辑工作，不能将日志内的模拟 ready 当作服务器 ready。
- 两份 150/200 窗口 factory 与源文件、50 个网络文件、3 个 vendor 文件、先前归档回执等
  全部通过原 SHA 门槛；原源码 manifest 共 474 项。冻结 focused 脚本 SHA 在导入前核验。
- 所有已检查的源码、CMakeCache、表数据及生产 `bin/ARCH` 在 CPU 测试前后字节不变。
  无公式、ODE、网络、容差、库或生产 CUDA 改动。

CPU 构建使用此前的 CPU-only、KLU-off、GCC 11、Debug 显式 `-O1 -g0` 目录，
只新增九个测试目标，`--parallel 1`、OMP/BLAS=1。不是标准带符号 Debug 内存验收。
构建 wall 约 36.07 秒、CTest 约 207.70 秒，只作诊断，不提供加速比。
`/usr/bin/time` 构建 peak RSS 561,068 KiB；护栏采样 peak owned RSS 597,280 KiB，
min available 105,441,648 KiB，swap 基线与峰值均 66,964 KiB，没有护栏中止。
两种 RSS 是不同测量口径，不应相互替代。

燃烧参考测试沿用其原有独立时间积分预算；不是 GPU 字段预算的替代品。
RKL/AMR 项是共享调度及表面计划合同，不是新增全应用扩散或设备 reflux 运行。

## 原始身份与准备失败

CPU 输入仍是[合并验证](../main-merge-20260917/README.zh-CN.md)的 `source-v2.tar`：
SHA-256 `24dcfae52cf22c0d1d5f78a82069cdf815b32ebdb4a284701f5f9a6344e52590`。
逐文件身份见 [cpu-v2/inputs.json](cpu-v2/inputs.json)，完整命令/退出码见
[cpu-v2/report.json](cpu-v2/report.json)，CTest 输出见 [cpu-v2/ctest.stdout](cpu-v2/ctest.stdout)。

首次 runner 在编译前发现合并验证目录没有实际 Helm 表而中止，
见 [cpu-guard-child-v1.log](cpu-guard-child-v1.log)。先前 Sod/Sedov 检查未使用该表。
v2 只补入服务器已有、与 Git LFS 指纹相同的表文件：
`c9a57c26c6fd2b2b378b9d5295ca1214022f6fec6289d038b47bf8c8938881a1`，60,242,514 bytes。
没有替换 EOS、重新生成表或放宽校验；v1 runner 和失败日志均原样保留。
这是准备问题，不是一次数值失败，也不计入成功测试数。

服务器采集包 [archives/server-evidence-v1.tar.gz](archives/server-evidence-v1.tar.gz)
共 39,407 bytes，SHA-256：
`450c85ab29a48f8bbffeacdf0c13733142b19fad68b25f1aa19a6c2ac60d3ecd`。
包内包含本次服务器 runner、原始 CPU 日志和输入身份、两次只读 GPU preflight。
下载后包 SHA 与逐文件字节核验见 `download-receipt.json`；展开文件在本目录。
首次本机回执生成把普通文件数误写为 14，实际为 13；包 SHA 和逐字节比较已经通过，
仅末尾数量断言失败。现场说明见 `download-check-v1.json`；更正后复核同一包，未重跑科学测试。
环境及十个 CPU 二进制 SHA 另见 `microphysics-resume-server-environment-20260918.log`。
表、生产二进制和源码不在此小包中重复存储，沿用既有 LFS 和合并验证归档。

## GPU 接续点

[window-preflight-v2.json](window-preflight-v2.json) 明确记录 `dispatch_blocked`：
ComfyUI（PID 689264）仍持有约 935 MiB GPU；即使快照 compute utilization 为零，
也未满足原空闲设备要求。没有停止他人进程，没有创建 focused 工作目录或启动工作器。
用户表示会安排空闲窗口；恢复时必须重新检查，不复用本次忙闲快照作未来授权。

下一轮严格沿用已冻结 `prepared-focused-input-v2.json` 及原运行护栏：

1. 150/200 核素 × BE_NR/BD/ROS4 六个 harness，2→3 单元、native pool=2、
   window=32，原四步到 1e-10，原字段/能量门槛。
2. 通过后再推进 32→33、pool 8/32、16 步长轨迹；各轮独立目录保存失败与重试。
3. 再做真实 Helm 全应用、合理 CPU 基线与正式重复计时；不能从微测试外推性能。

S5 扩散与内置网络既有验收保持原身份；新窗口方案仍为 validation-only。
合并主线完整 CUDA/采集接口 GPU 回归、超大网络端到端性能和 vGPU sanitizer 安全资格
仍未由本次工作补齐。
