# Linux 原生滚轮：被动事件投递诊断

基线 a2e4b9c5683bec06e440c441f0f0450f1a50f42c。
现有 production main/preload/dist，RT128×128，CPU binary ee3de6cf。
本轮临时入口只增加 document capture/passive 监听和定期只读状态采集。
没有 preventDefault、合成事件、sendInputEvent、自动点击、配置修改或 Core 演化。
UI 输入全部经 Computer Use，科学字段仍来自正式 init-only preview。

## 观察

观察器就绪时 events=[]，axes=x[0,.25]/y[0,1]。
原生最大化后，绘图区 scrollY=-480，没有记录 wheel。
同位置原生 click 记录 pointerdown/up：
isTrusted=true，target=svg，clientX1294/clientY696；sample9664正确选中。
随后普通参数区 scrollY=600，仍没有 wheel，普通参数滚动位置也没有可见变化。
同一个观察器能够收到真实 pointer 对照，因此不是整套监听均失效。

这将排查方向收窄到 Computer Use/Windows→WSLg/Electron wheel delivery；
尚不能判定其中哪一层负责，不能因此宣布真实硬件鼠标有故障或应用无故障。
与此前 renderer 注入 wheel 后缩放有效的集成证据分开登记。
native zoom/pan/fit 仍 NOT VERIFIED，不改物理/映射或门槛来绕过。

## 用户实际鼠标对照与生命周期

已请用户在当前窗口用硬件鼠标滚动，结果尚未收到。
截至本轮记录，owned Electron PID355883 live，exec session29013仍由当前任务持有；
窗口为用户对照暂保留，不声称本轮已经关闭或无进程。
测试结束后需正常关闭并复核所有 owned PID/start。
输入与binary SHA在当前时点未变；没有 Save/Build/Run/simulation/CUDA。
观察器、原始事件及本机身份记录位于 ignored：
studio/.local/integration/native-wheel-delivery-20261003。
源文件／摘要指纹可在同名 Summary.json 核对。
仅提交精简证据，不提交原始 H5/场数组/事件全量或本机观察器。
