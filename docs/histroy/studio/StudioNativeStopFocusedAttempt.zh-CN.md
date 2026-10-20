# 非最大化原生 Stop 验收尝试
2026-10-03，源码 2b4fc090628eac09a3d591e4e2039ad30066637c。本轮沿既有授权使用独立 ignored Sod 副本，只有 out_dir 更换；未延长物理终点、未修改科学参数或阈值。

从原生窗口显式打开输入，确认保存状态、compiled binary 和独立输出。窗口保持非最大化，重新选取唯一 Studio 窗口和新截图；点击 Start，重新激活后观察 running 和 Stop run，再点击 Stop run。没有对背景应用或终端输入。

运行 47e96a35-86aa-4ec1-9c81-3fa6c022dddf 从 2026-10-03T05:58:06.885Z 到 2026-10-03T05:58:25.243Z，终态 succeeded、exitCode=0、signal=null。点击后的页面显示 succeeded。因此未证明 Stop 导致停止，不能算 native Stop PASS；也不把点击本身作为行为成功。未自动重复运行。

worker/Core 已退出，现有 Electron/Host/warm Preview 的进程身份记录见同名 Summary.json。saved/frozen input 一致，binary SHA 与确认身份一致。保留日志终端不等于 orphan Core。完整输入、输出和日志留本机 ignored 路径，不上传。

3C 原生 Stop、active Run/Preview/AMR close 和显式覆盖保存矩阵仍待验收。本轮不改代码、不重复未变的 233 项检查、不 Build、不 push/tag/main merge，也不提前进入全模型/JENS/RZ。现有运行 Host 未重启，本次不验证最新 GNU 组件身份功能。
