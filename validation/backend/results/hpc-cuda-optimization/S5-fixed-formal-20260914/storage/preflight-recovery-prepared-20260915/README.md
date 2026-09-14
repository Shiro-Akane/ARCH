# 阶段间 preflight 保存与接续准备

本目录仅保存本机已测试的恢复工具；截至写入时，服务器的第二组
BE_NR/RKL2 正式矩阵仍在运行。这里不是第三组失败的证据，也不是新的科学通过记录。
尚未在服务器执行这些脚本，不修改正在运行的 v3 driver、worker、输入或二进制。

若后续 BD/RKL1 在采样前被资源门槛拒绝，应先人工检查真实日志与进程状态。
不能仅凭磁盘余量推断失败原因。已有 sample root、stdout 或 stderr 时，工具拒绝操作，
不能把已经启动或失败的样本当作 preflight 重跑。

`preserve-coupled-preflight-20260915.py` 只处理代码中写明的一个 timing scope。
它要求 worker 非零退出且已结束、没有 ARCH/编译/稀疏 harness、输出未启动；
逐文件记录 SHA-256，移动到全新 `preserved-preflight-...` 目录，再验证原字节。
不删除文件，不覆盖旧记录，不改变科学判定；中途失败保留已移动项和 pending move 日志。
它不声称自动诊断了 preflight 失败原因。

`run-remaining-coupled-after-capacity-20260915.ps1` 是一次性接续入口，不是定时任务。
只有旧本机 controller 已退出、前七个模块完整双备份审计通过、server 保存记录字节一致、
GPU/ARCH/编译均空闲且磁盘恢复到原 8 GiB 门槛，才依次调用原 v3 流程执行后四组耦合。
四组之后调用原 BE 延长 wall 预算补测入口；不重新运行已经完成的前七组。
脚本中的绝对路径是本次固定实验环境的运行配方，不是可移植安装器。

本机临时 fixture 的 13 项测试通过，覆盖正常保全、成功/活动 controller 拒绝、
样本输出拒绝、未知/缺失文件拒绝、旧目标拒绝、移动前变更和部分移动失败的日志与字节保留。
另已通过 Python AST、PowerShell parser 和内嵌 Bash `-n` 检查。
这些均不是服务器清理执行记录、CUDA 测试、sanitizer 或物理验证。

后续本机补充：接续入口同样拒绝活动的 sparse harness（包括尚未初始化 CUDA context 的 CPU 阶段），
读取保存记录时拒绝越界／绝对／反斜杠路径、缺失或变更的文件、错误状态／模块／pending move。
若 Python 用 `-O` 关闭断言，入口直接拒绝，不能静默绕过记录检查。
`test-coupled-resume-receipt-20260915.py` 直接抽取入口内的 Python 读取代码，13 项临时 fixture 测试通过；
较早的 12 项测试日志和增加 `-O` 拒绝测试后的 13 项日志分别保留。
这些补充仍未在服务器执行；正在运行的正式 worker、生产代码及计时协议不变。

如需空间，使用另一个已准备的 [S4 归档副本核验方案](../s4-curved-prepared-20260915/README.md)，
且必须在正式采样全部退出的阶段间隙执行。两项工具不降低任何资源或科学门槛。
