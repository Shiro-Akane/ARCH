# Linux 原生 Stop 尝试：未覆盖

2026-10-03，clean源码a68aee854307102a39de636923f2f3968a787244。
本次从独立Linux窗口原生选择器显式Open选择新测试副本，页面与Host关联一致，
Run预检确认已保存输入、CPU binary及新输出目录，再点击Start in independent terminal。
第一次选择器Return未更换关联，因此未用旧输入运行；显式Open后才继续。

配置复制既有4096-cell Sod UI测试输入，仅更换out_dir为独立ignored目录，
保持tmax=0.2/cfl=0.4及所有物理参数。不是新科学benchmark或阈值验收。
Run 8befed9e-b6cd-44e8-97e8-6ec44491e81b：
2026-10-03T05:45:14.747Z启动，05:45:30.313Z自然完成，exitCode=0。
真实窗口捕获了running、活动终端步进及Stop run控件。

点击Stop时Computer Use拒绝输入：
point (2498,1315) is over explorer.exe FolderView, not target msrdc Studio。
截图当时呈最大化布局；重新激活后实际窗口较小且run已经succeeded。
没有对桌面输入，没有绕过目标校验，没有重复Start、没有延长物理终点。
因此**native Stop NOT EXERCISED**，不能以自然完成或已有Host Stop证据替代。

worker26373/startTicks749053、Core26385/startTicks749060均已退出。
Electron14246、Host14292、warm Preview14445的PID/startTicks/父进程/组保持一致。
保留终端用于查看日志，不称其为orphan Core。saved和frozen input逐字节一致。
精简状态及指纹见StudioNativeStopAttemptSummary.json；完整输入/日志/output
仅在studio/.local/integration/native-stop-20bbee1d-5e30-4988-9617-ee4cfd0e6812
及studio/.local/runs/8befed9e-b6cd-44e8-97e8-6ec44491e81b，不上传原始数据。

本轮无代码修改/Build/CUDA，不重复未变的231项通过检查。
只对新增报告执行diff check。3C仍缺可靠原生Stop、active Run原生close、
active Preview/AMR close及明确overwrite等证据。下一次原生Stop应先确认窗口
真实几何与截图一致，或采用原生键盘焦点导航；不自动重跑本任务。
