# 原生 active Run 关闭与历史恢复

2026-10-03，源码 0ddc7ca6453bdbac59b693daac41c703b395a5a8，开始前工作树clean。沿已授权4096-cell Sod测试输入，仅换新out_dir；tmax0.2/cfl0.4和物理输入不变。通过原生选择器打开、Run预检、显式checkbox和Start启动独立终端，700ms后捕获running及真实步进；随后点击原生Studio关闭按钮。没有发送进程信号、没有关闭xterm或改写工作副本。

Run 76f06645-6aa0-4d9d-b9df-f3b4f637fd83 在 2026-10-03T06:16:29.118Z 开始。desktop.log记录Host35350 exited及desktop clean shutdown于 2026-10-03T06:16:37.242Z；Run在 2026-10-03T06:16:46.612Z 才自然succeeded/exit0，晚于关闭 9.370 秒。因此本项证明关闭没有取消已交付计算。关闭后只读进程观察时任务已结束，不声称捕获关闭后的live worker或日志增长；历史未覆盖尝试不改判。

旧Electron35303、Host35350、warm43430均退出；Run worker53400/Core53412结束后均退出。日志终端保留符合既有策略，不将它作为orphan Core。保存输入和frozen input一致，binary指纹不变。

同一Linux正式入口重开，新窗口93457630正确关联项目/测试副本/Sod1D。原生Saved run history显示本次Sod succeeded以及此前native Stop的stopped记录；新Host读取同一持久记录，state逐字段相同。恢复只读取历史，不重复运行或停止已完成任务。

本轮只有验收和报告，无源码改动、不重跑234项检查、不Compile ARCH、不CUDA。完整输入/output/H5/checkpoint/logs留本机studio/.local/integration和studio/.local/runs；提交精简状态/身份摘要与清单。没有push/tag/main merge。native active Run close可标通过，active Preview/AMR关闭和明确overwrite等其他矩阵仍待完成；不宣告整个3C或联合科学计划完成。
