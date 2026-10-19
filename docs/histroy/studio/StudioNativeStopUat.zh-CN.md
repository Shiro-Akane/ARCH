# 原生 Stop 与 Run 确认面板验收

2026-10-03，运行源码 cb3108df9cef7408c016c64bc83fff33270808b5。使用既有CPU4096 Sod输入，tmax0.2/cfl0.4不变，仅新out_dir。未扩大物理终点。原生选择器打开独立副本，身份和预检确认后Start；一次输入后等待700ms再捕获，观察真实running和Stop run，直接点击Stop；未对xterm输入、未用Host API代替原生按钮。

Run 83b1f0ec-dadb-4122-8d3f-2507364f79c0，2026-10-03T06:11:12.579Z 至 2026-10-03T06:11:18.185Z，终态stopped/SIGTERM。UI依次显示stopping、Latest run stopped；worker44329/Core44341均从/proc消失。Electron35303/Host35350身份不变。日志终端保留符合既有策略。saved/frozen input完全相同，binary SHA保持e506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7。本项可标native Stop PASS；历史自然完成尝试仍保留原未覆盖结论，不追改。

实际UAT发现日志抽屉打开后覆盖Run确认面板：两个absolute sibling默认堆叠且drawer后绘制。仅为.run-confirmation加入z-index:1。没有改变Run确认门槛、请求、生命周期或科学代码。生产窗口刷新后保持drawer打开，prepare Run显示完整确认、复选框、Start/Cancel，点击Cancel返回drawer；未勾选或再次运行。既有输出目录提示保持可见。

最终Studio/Host234/234、lint、typecheck、production build、diff-check PASS；已有bundle warning保留。不添加镜像CSS的测试。完整日志留studio/.local/integration/run-confirmation-stacking-regression.log。

本次刷新沿既有行为执行init-only Preview，非simulation。所有本次Run输入/H5/checkpoint/完整日志留本机ignored目录；仅提交精简摘要。3C仍需active Run原生close、active Preview/AMR close和明确覆盖保存等证据；不以Stop或CSS修复宣布整个计划完成。未Compile ARCH/CUDA，无push/tag/main merge，不提前进入全模型/JENS/RZ。
