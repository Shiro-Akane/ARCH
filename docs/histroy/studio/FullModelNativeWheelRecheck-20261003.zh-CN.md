# 原生 RT 输入复核：点击有效、滚轮未验证

基线：61cf9142d70edeb333a260553a4e360fd32db8cf。
现有 Linux production main/preload/dist，CPU binary ee3de6cf，
RT 128×128 初始预览，domain x=[0,.25]、y=[0,1]。
本轮仅原生操作，不用 renderer 注入事件替代验收。

首次启动测试命令把 Electron flags 放在 Studio CLI 后，严格校验拒绝，
显示 Open project 页；正常关闭 exit0 后，把 flags 移到入口之前重开成功。
该错误属于本轮测试命令，未修改 production CLI。

新 returned window id2949186；先激活，再原生双击标题最大化，
工具返回2560×1392工作区。原生点击 plot(1300,750)：
sample9664，i64/j75，x=.1259765625，y=.58984375，Density2。
截图 Inspector 同时显示 build39eb4f9e/config aa72d96c240e/binary ee3de6cf2b8c。
这提供点击／命中链路有效的额外证据。

发送负滚轮-480；点击确认选点后发送正滚轮+600；
即时和后续稳定观察均未见坐标域改变。
仍不能判定应用、WSLg或工具哪一层负责，native zoom/pan/fit **NOT VERIFIED**。
不据此调整数学，不以既有 renderer 集成 PASS 替代此项。
下一步应采用被动事件记录确认真实 wheel delivery，禁止合成 wheel 冒充原生结果。

原生 Alt+F4 正常关闭，launcher exit0。
关闭前记录的8个 owned进程按PID/start全部消失，输入与binary SHA不变。
无 Save/Build/Run/timestep/CUDA，其他窗口未操作。
本机 ignored 日志／进程记录在 studio/.local/integration/native-wheel-recheck-20261003。
全模型桌面出口未完成；本轮源码未变，不重复自动回归。
