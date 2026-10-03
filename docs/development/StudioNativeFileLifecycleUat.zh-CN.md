# Linux 原生文件生命周期：部分工程 UAT

2026-10-03，源码基线 4df8367a94a197b0b3d54953eedaa9a07dc94110。
本次通过已有 Linux launcher 与 production assets，使用原生 GTK 文件选择器，
没有浏览器自动化、源码修改、重新构建或新 Run/Restart。
完整身份见 [精简记录](StudioNativeFileLifecycleSummary.json)。

## 实际操作与结果

| 操作 | 观察及核对 | 覆盖结论 |
| --- | --- | --- |
| 旧窗口正常关闭、新 launcher 启动 | 旧 Electron5388、Host5434、warm5586及已结束Restart worker/Core均退出；新 launcher14246、Host14292 | idle 关闭／重开工程证据；不是 active Run survival |
| 原生 Open 后 Cancel | 主窗口仍关联 studio/.local/UatSod.par，Saved/Disk in-sync | 本次取消路径通过 |
| 搜索并编辑 CFL | 工作副本0.4→0.41，Dirty；Inspector区分工作值0.41和已保存值0.4 | 工作副本编辑可达 |
| Dirty 时 Open | 先显示保存／另存／放弃／取消替换选择 | 未静默覆盖编辑 |
| Cancel replacement | 原文件关联和未保存0.41保留，Dirty仍在 | 本次保护取消路径通过 |
| Save As and continue | 原生 Save 对话框输入新的空格／中文路径，显式Save后进入下一Open选择器 | 原生保存链已执行 |
| 文件核对 | 新文件1260字节，只将 cfl = 0.4 改为 cfl = 0.41；原文件1259字节及SHA不变 | 未规范化其他输入，也未插入缺失diff_cfl |
| 打开原文件、再 Reopen 新文件 | 原生路径框与显式Open；最终header/Host关联新副本，CFL0.41、Saved、Disk in-sync | 实际读取保存副本，不用同文件打开冒充更换验证 |
| 新 catalog wording | 实际页面显示102 catalog keys，未称其为标准参数数量 | 加载了修正后的production assets |

本地新副本：studio/.local/Native UAT 中文 Sod 20261003.par，
SHA-256 f452da1cb53bff2a7fe9c6c5d62407e979e5c88893f2a1a231f2c26b7c1083fb。
原 UatSod.par SHA-256仍为
381ceb60eb6cdbf7c99b43577ff0d94731144d655514c7a8194d09ce8d88644e。
仓库原Sod输入与受管ARCH binary SHA均未改变。
修改后的副本只用于文件UX，未用它启动演化或覆盖已有output。

编辑和重开触发现有300 ms自动初态预览策略；短暂Previous/stale随后Current。
源码 RealInitWorkspace 按完整Working Copy文本比较，接受新request后才Current；
没有关闭或改变自动策略，也不能将本轮描述为“未执行任何Preview”。
这些初始化调用不等于simulation timestep。自动套件没有新改动或失败证据，
本轮未重复已通过的229项Studio/Host检查。

## 显示与验收边界

工具捕获到了独立WSLg窗口（id13437218，最大化2560×1392），不是Codex侧边栏。
但用户最近确认仍只有任务栏图标；新窗口的用户可见性回答仍待确认。
捕获和激活不能证明用户桌面可见，**完整Linux原生桌面验收未通过**。

空格和Unicode路径实际保存／打开成功，中文字符在GTK和Studio中显示缺失字形方框。
这只能证明路径字节和文件操作，没有证明中文名称可读；保留Linux字体显示问题，
没有把它改为Windows适配任务或通过更名规避。

未覆盖：Save As选择器Cancel、Discard and Open、显式覆盖／外部冲突UX、
active Preview/AMR关闭、active Run独立存活与显式Stop、完整用户可见桌面UAT。
旧已完成任务的终端仍按设计保留日志，不是活跃Core的存活证据。

原始输入、日志及科学数据仍保留本机忽略目录；只提交本报告和精简指纹。
完整联合目标与阶段顺序不变；没有push/tag/main合并，也未进入全模型/JENS/RZ/CUDA阶段。

## 2026-10-03 补充：干净 Working Copy 的原生 Save As Cancel

执行前源码 fbb0fe3bbc0bff2e1e5ed63622d85d7977e7d3f5、working tree clean。
同一个Linux独立窗口13437218；从实际参数面板底部
Save Working Copy As 打开GTK保存窗口78253042。
窗口标题 Save new project configuration，默认名称 StopSod4096_copy.par，
目录为managed project root。实际点击原生Cancel；
后续list_windows只剩Studio、不再返回该模态，主界面恢复。

Host保持原projectId与StopSod4096.par关联，输入SHA
1ab5c596b7f6d2225fceebdc06847c3d66a5b40610f1c30d87f8f57059404cef、
mtime/size未变化；默认新目标不存在；选定binarySHA仍e506619f...。
取消没有落盘或切换关联。主界面仍为已保存状态，既有Preview保持显示；
本轮未编辑参数、启动Preview/Run、重新Build或改源码。

此项只证明干净工作副本SaveAs Cancel；Dirty副本取消另列未覆盖。
工具原生交互与窗口捕获不替代用户独立窗口可见性确认。
原生覆盖/外部冲突/活跃close等矩阵仍未完整，整体3B/3C未宣称PASS。
仅补充处理后摘要与文档，git diff --check；不重复 unchanged suite，
不上传原始数据、不push/tag/main merge。

## 2026-10-03 补充：原生外部冲突、Dirty Cancel 与另存恢复

执行前8f2a30af0ae0be2eb16b5f1b18ba6b3104337ea5，工作树干净。
新建本机ignored目录 native-save-conflict-2a662c9f-ff83-44de-b2a6-896ea0644358，
从此前有效UatSod.par复制Sod.par；历史输入/原始工程Sod不改。
原生Open13044318精确关联新副本。Core目录搜索cfl后，
实际文本控件0.4→0.41；Inspector Working Copy0.41与Saved0.4分离。

独立外部写入只向测试副本追加一行注释，保留cfl0.4。
原生Save实际被拒绝；主界面Disk changed-externally，并明确显示
“This configuration file changed on disk. Your unsaved Working Copy has been kept.”
工作副本0.41仍在，磁盘保持外部改动SHA
bb76a91310d870ed661f196c0e1de82cc5226b2b2cdd278a1ae64bd0790263ca。

从该冲突提示打开GTK Save As37358706并Cancel；
模态关闭后Inspector仍WorkingCopy0.41/Saved0.4，冲突提示保持，
默认Sod_copy.par不存在。此项补齐Dirty SaveAs Cancel，未丢编辑。

随后再次打开GTK SaveAs125438558，明确选择此前不存在的
同目录RecoveredSod.par并Save。新文件真实落盘1260bytes，
SHA f452da1cb53bff2a7fe9c6c5d62407e979e5c88893f2a1a231f2c26b7c1083fb。
逐字节等于原始UatSod内容只将cfl = 0.4改为0.41；
缺失diff_cfl未插入。外部注释仍在冲突源文件，不被新副本吸收或覆盖。
Host/header关联RecoveredSod.par；界面Saved、Disk in-sync；
原UatSod与binarySHA均未变。

已有自动init-only Preview随工作副本编辑/新关联刷新；没有Run/Restart、
科学timestep、Build或科学源码变更。仍不以工具原生捕获代替用户
独立窗口可见性；最后用户只有任务栏图标的反馈未被新回答取代。
本轮关闭外部冲突拒写/保留、DirtyCancel、另存恢复项，
并未覆盖明确Overwrite选择或活跃窗口关闭矩阵，不宣称整个3C完成。
只提交处理后摘要/报告/进度；原配置与完整本地证据留ignored目录，
diff check通过，不重复既有unchanged229-suite，无push/tag/main merge。
