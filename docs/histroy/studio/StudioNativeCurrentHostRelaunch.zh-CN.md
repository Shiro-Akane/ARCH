# 当前源码 Linux 桌面暖会话关闭与重启

2026-10-03，源码 40dd783b50c5c382d1c2dae80ac22b0fc6df31da，开始前工作树 clean。

当前窗口 Saved、Disk in-sync、Preview Current，Run history 无活动任务。通过原生 Studio 关闭按钮关闭，旧 Electron14246、Host14292、warm Preview14445 均从 /proc 消失；desktop.log 记录 owned Host exited 和 desktop clean shutdown。未通过信号或 PID 强制关闭。

从已有 Linux arch-studio entry 重新启动同一项目/CPU binary/已保存测试副本；未启动 Vite、未安装软件、未 Configure/Build 或 Run。新 Electron35303/start857946、Host35350/start857958、warm worker35775/start858688。Host parent 为本次 Electron，worker parent 为本次 Host，PID 均有 startTicks 证据。

新的原生窗口15730862显示正确项目、Sod1D、Saved、Disk in-sync、Preview Current 和 x_pos0.5。生产页面启动沿既有流程执行 init-only Preview，身份为2fd9a2cf-38f5-4557-b9ea-fbbf198bf781；configRevision 和 binarySha256 与磁盘相符。没有运行 simulation，新旧 binary 指纹一致。

新 Host 已加载当前工具链前后稳定性实现，但历史成功Build Manifest没有该证据，build状态如实显示 freshness-unknown，原因Compiler toolchain identity is incomplete or unavailable；不伪造新Manifest、不追溯标记当前源码已构建。UI Build ready只表示profile可用，与binary freshness不同。

本项证明 idle warm-session 的原生正常关闭、进程清理和当前源码重启；不替代 active Preview/AMR、active Run关闭、原生Stop或覆盖保存矩阵。窗口可见证据来自工具捕获，本次没有要求用户重复确认既已确认的Linux可见性。

只有报告和清单提交；原始日志和输入在 studio/.local/integration/native-relaunch-40dd783b，旧科学输出仍留本地。未改代码、不重复234项检查、未编译ARCH、无CUDA/push/tag/main merge。
