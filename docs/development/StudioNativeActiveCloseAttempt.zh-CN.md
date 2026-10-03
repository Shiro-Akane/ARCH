# 3C 原生活跃 Run 关闭测试：未覆盖记录

2026-10-03；执行前 clean commit 10ad80b1a28394dc4f49591c8f94a3c1f0910b60。
本次通过已有 Linux Studio 原生 Run 准备、明确确认与一次
Start in independent terminal 操作启动 Run，不是 API 替代点击。

Run afb95966-335d-47d4-8620-39d4ca6f8035 已自然完成：
04:57:55.833Z → 04:58:12.341Z，exit0，step8994，time0.2。
意图是在活跃计算时关闭 Electron，但恢复观察时计算已结束。
没有执行活跃 Run 关闭，也没有为获取 PASS 自动重复计算。
因此该项为 **NOT EXERCISED**，不是 PASS，也不是失败后修复通过。

固定网格 CPU Sod4096 cells，CFL0.4，tmax0.2，maxAMRlevel0；
仅使用新的 ignored 本地 out_dir，沿用已有有效物理输入。
冻结 input.par 与保存配置逐字节一致；配置与 binary 的 SHA、
进程身份和本地原始输出索引见 Summary.json。
worker19122/Core19134均已不存在；terminal19121按-hold设计保留。
原始 Sod 与 UatSod 输入及 selected binary SHA保持不变。
不将日志中的零 floor repair 记为独立科学准确度验收。

用户最新回答仍为“只有任务栏图标”；此后工具置前并最大化
已有独立窗口，捕获到参数界面，人工显示确认仍待回复。
工具捕获不能推翻用户报告，用户可见性 **NOT VERIFIED**。
本次没有关闭或重开当前 Electron/Host/暖 Preview，也未重启WSLg。

既有生产 Host 关闭后 Run 存活及重开 Stop 的证据独立保留；
不能用它替代本项原生 Electron 活跃关闭测试。
3C原生活跃 Run/Preview/AMR关闭、原生Stop、显式覆盖与可见性仍未完成。
架构审计迁移和历史非物理 G 输入转换批准仍待处理；
不推进全模型预览、Jeans/RZ或CUDA验收。

原始H5/plt/checkpoint、完整输入与日志仅保存在本机 ignored目录。
只提交处理后摘要和本报告；不改源码、不Build、不重复未变测试，
不push、不tag、不merge main，不开展Windows适配。
