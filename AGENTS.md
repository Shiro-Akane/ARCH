# ARCH 工作入口

工程实现、审查、验证或性能工作使用项目 `arch-engineering` skill；需要外部协助时结合全局 `multimodel-engineering`。沿用现有物理定义、数值标准和验收，安装这些规则不授权源码或架构优化。

## 本机路径与执行入口（2026-10-12 已核实）

- 本项目在 WSL Ubuntu 中执行，shell 为 `/bin/bash`。Windows C 盘文件通过 `/mnt/c/Users/26428/...` 访问，切勿将 `C:\Users\26428\...` 直接交给 Linux 命令。
- 当前工作区为 `/home/shiroakane/.codex/worktrees/d7a6/ARCH`，主仓库目录为 `/home/shiroakane/ARCH`。恢复上下文后先读取本文件和 `ARCH_MODERNIZATION_PLAN.md`，再用 `git status --short --branch`、`git worktree list` 确认实际分支和目录，不凭旧聊天推断。
- 若普通 `exec_command` 在启动进程前报 `CreateProcess ... No such file or directory (os error 2)`，不要反复替换 bash／PowerShell 路径。已验证的恢复入口为：`shell="/bin/bash"`、`login=false`、真实 WSL `workdir`，并通过工具审批使用 `sandbox_permissions="require_escalated"`。审批拒绝时按拒绝理由处理；本记录不授权绕过审批或永久关闭沙箱。
- Windows `node_repl` 会拒绝 `file:///home/...` 的 `sandboxCwd`，这不证明文件不存在。WSL 项目文件须通过上述 WSL 命令入口读取，不在该 Windows Node 会话中反复尝试 Linux 路径。常规沙箱启动与 Windows Node 路径兼容问题尚未根治。

## Python 与 Studio 工具环境

- Python 默认使用已有 conda `work` 环境。交互 shell 直接输入 `work`（已核实为 `conda activate work` 的别名，对应 Python 为 `/home/shiroakane/miniconda3/envs/work/bin/python`）。缺包可在这个环境内安装，不要重复搜寻或新建替代环境。
- 自动化命令需要别名时使用 `bash -ic 'work; python -c "import sys; print(sys.executable)"'`（非交互 bash 不保证加载该别名）；也可按既有环境指南显式初始化 conda 并激活 `work`。
- 已核实 `work` 的 Python 3.12.14、NumPy、h5py 可用。该 Python 当前缺少 `os.pidfd_open` 和 `signal.pidfd_send_signal`；资源护栏及必须使用这些 Linux 进程接口的工具沿用 `/usr/bin/python3`（已核实提供这两个接口）。此例外不改变科学 Python 默认使用 `work`，也不能用安装普通包代替标准库接口检查。
- Studio 使用 Linux Node 24+。本机现成入口为 `/home/shiroakane/.local/opt/node-studio/bin`（已核实 Node 24.21.0），运行 npm 前将整个目录前置到 `PATH`。切勿只调用该目录的 npm 而仍让其选中 `/usr/bin/node` 18。详细环境要求见 `docs/guides/StudioEnvironment.zh-CN.md`。

## 进度恢复

- 现代化计划与工作包入口：`ARCH_MODERNIZATION_PLAN.md`；本轮前置记录：`docs/development/Preflight-20261012.zh-CN.md`。
- 既有科学验收以 `docs/development/ComputeOptimizationPlan.zh-CN.md` 顶部 R1–R6 和 `ComputeStudioReleasePlan-20261006.zh-CN.md` 的当前出口为准。历史空复选框不等于待重做任务；已通过项按其源码、执行物和输入身份复用，缺项、失败、中断与后置分开记录。

- 新工作区测试前核对 Git LFS 夹具：`studio/tests/fixtures/sod-1d.h5` 应为真实 HDF5，而非文本指针。本轮 `git lfs checkout studio/tests/fixtures/sod-1d.h5` 从本地对象库恢复成功；对象缺失时仅拉取需要的路径，勿顺带下载全部 EOS／历史模拟数据。
