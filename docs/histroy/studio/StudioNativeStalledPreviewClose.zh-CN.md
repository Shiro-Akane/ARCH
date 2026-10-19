# Linux 原生关闭活动 Preview：受阻 worker 清理通过

2026-10-03，基线 cbc66c49dc9314859d887ecc70af70a829ef2c15，开始工作树 clean。
本项是明确标记的进程故障注入验收，不是新的科学验证。

## 普通 warm 尝试与边界

Linux 正式 launcher 加载既有 CellularDet t=0 AMR 对照配置，
production 页面启动沿现有流程生成128×128真实初始化视图。
状态 API 实测读取0.77秒；上一轮0.5秒监测 timeout确实不足。
改用5秒监测并分离 /proc 轮询后，捕获 warm request
e5cee974-b5ec-4144-8616-532c8bfba7ac 为 generating；
但06:30:08.673Z已观察succeeded，原生关闭请求06:30:10.265Z才发生。
因此普通 warm active close 仍未覆盖；不能把先前UI截图代替时序证据。
Electron69047、Host69094及worker69517正常退出。

## 明确故障注入与实际原生关闭

从同一现有Linux入口再开，同一保存配置和CPU binary。
先等待startup init-only Preview succeeded，再严格核对本次Host71947的唯一
直接ARCH子进程72375/startTicks1005444、真实 executable路径和
--preview-session参数。只对这个idle Preview worker发送SIGSTOP。
未暂停Host/Electron、模拟计算或其他应用；设置90秒保护，
仅当同PID/startTicks仍处于T状态时自动SIGCONT，防止验收中断留下暂挂进程。

通过原生Update Preview按钮发起一次请求1272c28f-8d6f-4d9d-91ea-15fca3f46c45。
状态API持续返回generating，session stage=request；06:32:46.038Z直接只读
复核worker仍T、保护未恢复。原生截图也显示request/generating并保留上次热图。
然后点击Studio原生关闭按钮，未使用Host mutation API或手动kill替代关闭。

desktop.log：
- 06:33:14.327Z shutdown requested
- 06:33:15.348Z owned Host exited
- 06:33:15.349Z desktop clean shutdown

Host约1.021秒退出；/proc监测和关闭后复核确认
Electron71901、Host71947、worker72375均消失，90秒恢复保护未介入。
源码PreviewSession.fail先SIGTERM、1秒后SIGKILL；观测耗时与既有grace一致。
没有收集worker退出码，不额外宣称直接观测到其SIGKILL exitCode。

**活动受阻Preview请求原生关闭及owned进程清理：PASS（fault injection scope）。**
证明的是未完成请求和不响应worker的清理，而非Core正在自然Setup/Init计算的瞬间。
普通warm尝试仍按原始证据保留未覆盖，不追改。
AMR active close和完整3C仍未通过。

## 身份、数据与检查

输入保持既有tmax=0/use_burn=false及全部参数，不编辑、不Save、不Run/Restart。
config SHA45b21a327cc54f974be506dda3b4db808b29d9b10110e06cc21b91d5f2184086；
binary SHAe506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7。
两者前后完全相同；Build仍f6449e3d-ccc0-4d51-ac96-eaf767de62a8，
dependenciesComplete=false不变。没有Build、Core修改或CUDA。
暂挂进程只改变测试调度，不改变科学定义、输入、阈值或代码。
该项不产生新的物理演化证据。

完整状态监测/保护脚本保留ignored目录
studio/.local/integration/native-stalled-preview-close；
普通warm记录在native-preview-close-recheck。
提交仅精简请求/进程身份、时间和验收范围，不包含raw data/完整场数组。
本轮产品代码未变，只检查JSON格式及git diff --check，
不重复234项未变实现的通过测试。无push/tag/main merge。
