# S0：共同基线、工具审查与批量接口交接

日期：2026-09-12。状态：**开发交付／待共同审查，不是已验收里程碑**。
唯一当前优化计划：[HpcCudaOptimizationPlan.zh-CN.md](../../../../../docs/development/HpcCudaOptimizationPlan.zh-CN.md)。本目录保存内部证据，不新建科学验收标准。

## 身份和边界

- 共同主线：`Shiro-Akane/ARCH main@7d4448a9b4a07dd86aa8cfc83fe9d85c7d63a866`。
- 原服务器实验源码：`0266d96f20b184d4b17ebc6a066ac3b9021f1642`；相关生产/构建/测试工具与共同主线无差异。新旧实验均保留各自真实 SHA，不重标旧数据。
- 本地与两方固定分支：`codex/hpc-cuda-optimization`；个人 `Arsenic-er/ARCH`，朋友 `Shiro-Akane/ARCH`。不更新 main、冻结 V1 或正式 Release。
- S0 不修改 `src/`、数学、provider、浮点开关或科学容差。四个既有 harness 文件及新增 S0 工具控制单独审查。
- 当前功能范围冻结在上述 main；未来主线新功能先明确集成截止 SHA 和范围。接口/科学判据需维护者与相关调用方确认，不代签。

## 证据入口

| 记录 | 用途和验收范围 |
|---|---|
| [原性能汇总](../../h100-performance-20260909/README.md) | 同机 CPU8/CUDA 两档 Sedov 历史观测；非裸机 H100，非维护者已独立复核 |
| [原完整 timing JSON](../../h100-performance-20260909/timing-report.json) | 16 次运行、16 项比较、参数、工作量、场量/守恒、源码/二进制/工具身份 |
| [独立 API 观测](../../h100-performance-20260909/profile-report.json) | 包含等待的 Host API 数据，不是纯 kernel 时间或保证可消除的开销 |
| [focused 编译日志](../../h100-performance-20260909/focused-build-completed.log) | 4 个 audit150/200 目标的编译/链接证据，不是科学轨迹 |
| [完整 ARCH 编译日志](../../h100-performance-20260909/integration-build-completed.log) | archive/应用链接退出 0；不是完整 GPU 验收 |
| [中断状态快照](../../h100-performance-20260909/campaign-status-20260912.json) | 保留原失败与空 commands，不修改成通过 |
| [工具差异审查](ToolingReview.zh-CN.md) | 四个既有文件的动机、判据影响和待补项 |
| [batch 接口草案](BatchInterface.zh-CN.md) | S1 最小变更、MPI/物理交界和实际测试入口；待共同确认 |

可直接复现的原配方在 [run_sedov_amr_timing.py](../../maintenance-freeze-20260908/run_sedov_amr_timing.py)。本次复跑入口为 [run_s0_checks.py](run_s0_checks.py)，仅委托现有 runner/provenance，不另写数值比较器。

旧完整原始数据包 SHA-256：`7eb438af5e5a47a7e15b069987e945808592150c1bbd76c5d8bc2a5b9c71648b`。它在服务器源码目录的 `build/performance-20260909-evidence.tar.gz` 和本地相同相对路径保留。授权服务器用户可取回：

```sh
scp ubuntu@100.97.101.5:/home/ubuntu/projects/ARCH-perf-20260909/build/performance-20260909-evidence.tar.gz .
sha256sum performance-20260909-evidence.tar.gz
```

这要求已有 Tailscale/SSH 权限，不是匿名下载链接。源码、紧凑报告与输入进 Git；HDF5/二进制/完整 profiler 数据不进入普通 Git。未授权使用服务器的合作者仍需安排归档传递，不能只靠不可访问的绝对路径。

## 当前实施情况

1. 两端仓库 URL、main SHA、固定分支不存在及 push 权限已核对；本地固定分支已从共同主线创建，旧工作区改动保留。
2. 原两档基线在服务器独立新目录复跑；未与编译、大网络或其他 GPU 作业并发。机器：H100-20C vGPU 20 GiB、117113 MiB RAM、32 vCPU；启动检查 GPU 0 MiB/0%，RAM available 113063 MiB。最终数字以保存的本次报告为准。
3. 本地 S0 编排负向控制 10 项和 sparse parser 9 项通过；全套 Linux tooling 及本次复跑结果另附，不以本地小测试替代。
4. 大网络旧脚本用 `artifact-sha256.txt` 的存在作为整体构建成败代理，导致错误归类。原 post-build 流程为何在子构建退出 0 后中断尚无完整 stderr 证据，不断言 OOM/编译失败。新的检查记录把产物身份、build dry-run、汇总和科学运行分开；不重新运行旧自动部署脚本。
5. vGPU sanitizer 未验收；本次不改驱动安全设置。本机/其他支持调试的 GPU 补测地点待安排，不假定某机器现在可用。

## S0 交付门槛与待确认

| 维度 | 当前状态 |
|---|---|
| 正确性 | 本次原 Sedov 16 次运行/16 项比较通过；新增 S0 10 项和 sparse parser 9 项通过；Windows 全套存在失败/跳过，Linux 新提交待复核；没有生产优化验收 |
| 性能 | 本阶段只复现基线，不声称加速；S1/S2 才比较候选收益 |
| 安全 | GPU debugging 限制，待其他环境补测；S0 工具负向控制不是设备安全证明 |
| 接口 | batch 草案已形成；MPI 局部/全局 finalize、ghost ready/consumed、owner 变化以及物理能力规则待负责人共同确认 |
| 集成 | 开发交付，可供审查/补测；不建议当成已验收生产优化合并 |
| 同步 | 将同步开发交付，不打验收标签；实际提交 SHA 和双端回执以 Git 与任务回复为准 |

S1 前需确认最小公共 API、功能截止范围及性能判定口径。拟预先采用：至少 5 次交替测量，报告所有样本；任一原两档中位数退化超过 5% 时触发追加独立测量及分析，不能直接宣称通过。5% 是待审的回归调查触发线，不是放宽科学预算或预先保证的显著性判据；收益需超过实测离散性。

## 复跑与恢复规则

新实验只用新输出目录；不覆盖已有非空或空目录，不盲用旧完成标记。恢复先读最新状态和现有输出：已完成记录保留，缺失阶段用新的 attempt 目录显式执行；源码/输入/配置改变则冻结新的身份重跑受影响范围。拒绝部分矩阵、重复 pass、控制参数错配和缺失产物，复用现有工具负责判定。

构建检查通过只说明当前产物/配置可观测；`build_graph_up_to_date=false` 时不得自动复用或自动重编，先审阅 dry-run。科学与 sanitizer 结果另列，不依赖一份 marker 文件推导整体 pass。

## 本次实测回填

[本次完整基线报告](evidence/baseline-timing-report.json)和[执行状态](evidence/baseline-status.json)：预热 1 次、正式 3 次，16 次运行和 16 项比较全部通过；场量、守恒、物理时间、步数/regrid 工作量及前后身份检查通过。

| 基础块数 | 旧 CPU / GPU 中位数 | 本次 CPU / GPU 中位数 | 本次 CPU/GPU 比值 |
|---|---|---|---|
| 4×4 | 13.953 / 17.890 s | 13.274 / 17.692 s | 0.7503 |
| 8×8 | 70.622 / 87.765 s | 70.610 / 88.133 s | 0.8012 |

这里没有生产优化，不能把两天间的波动算作改动收益。GPU 仍分别慢约 33.3% 和 24.8%；大档 CPU 样本 68.343–72.851 s、GPU 85.081–88.228 s，后续候选需足够重复和同轮对照。原 runner 的 `speedup_qualified=true` 表示比较有效，不表示 GPU 一定更快。

原参数文件可在 [old-inputs](evidence/old-inputs/) 审阅，包含 16 份基线输入和 4 份 API 观测输入；文件内容未修改，含旧运行路径，重跑请使用原 recipe 生成新目录。四文件 diff 为 [harness-against-main.patch](evidence/harness-against-main.patch)。

本次基线使用的编排版本保存在 [baseline-recipe-used.txt](evidence/baseline-recipe-used.txt)，其身份在状态 JSON 中。随后开发提交 `f9688c87` 补了异常状态记录和 dry-run 参数防错，不把旧运行脚本 SHA 改成新版本；原 Sedov recipe/源码/二进制始终不变。

### 工具与环境限制

工具扩展提交 `13e74e59`，S0 控制提交 `f9688c87`。Windows 全套发现 317 项，9 errors、24 skipped，详见[原日志](evidence/windows-tooling.stderr.txt)与[状态](evidence/windows-tooling-status.json)。主要涉及缺少 Linux `os.sysconf`、符号链接权限、SQLite 文件锁、路径/编码和受控环境里的 Git 查找；不修改这些不相关工具以制造通过。架构审计退出 0，S0 新控制 10 项通过；sparse parser 9 项单独通过。Windows 全套不是通过，仍需 Linux 无跳过复核。

本机 WSL 未列出已安装发行版，不在本轮擅自安装。服务器 GPU 在本次基线完成后空闲，但新开发提交传送被安全审批拒绝。只读 Tailscale 核对显示 `100.97.101.5` 的 HostName 为 `gpu-273312`、与本机同所有者；审批仍要求用户明确授权向该地址传送本项目代码包。不会用另一上传方式或间接 pull 绕过拒绝；服务器新代码/全套测试及大网络扩展暂停于该边界，现有结果照常保全。

反馈文件 SHA-256：`a5e6a3a5f2932f2ef7846de948153bb51be6e89d0624a5f49f59ef6ef9276d44`。接口共同确认、性能口径确认与支持调试 GPU 的补测地点仍待负责人回复，因此本交付不标 S0 已验收，也不直接进入 S1 公共接口改动。

### 大网络产物核验（只读）

[完整身份报告](evidence/integration-build-inspection-complete.json)记录原 `0266d96f` 工作区与当前构建/库/生成包/二进制身份，前后稳定。ARCH SHA-256 为 `5b14b5454ae5570e0334b99e17cfa1680b245c9a3d3dae2ac5ce0d1ee80cd4bd`，archive 为 `77eb0794d7744fbf85973eade85f320e9d8de040c01065b4f13af567ba18dee8`。

[dry-run](evidence/integration-build-dry-run.log)要求重新运行 CMake，故 `build_graph_up_to_date=false`。检查流程退出 0 只表示成功收集状态，**不表示构建图无需更新或科学验证通过**。下一步先受控检查 CMake 再生成的原因和差异，再决定产物是否可复用；本轮没有启动重编或大网络轨迹。

证据文件的 [SHA-256 索引](evidence/file-sha256.csv)用于下载后核对。归档目录禁用 Git 自动换行转换以保留原始字节；编译器日志自带行尾空白/Windows 日志 CRLF 原样保存，不为让 whitespace 检查变绿而修改证据。
