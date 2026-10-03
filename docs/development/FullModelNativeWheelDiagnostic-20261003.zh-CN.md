# Linux 原生滚轮与输入目标诊断

本轮基线 8d8e661793d2033d52067fedf43ed1cd3bb72e59，真实 production Linux Studio、RT 二维初态、128×128。
没有修改源码、配置或科学规则，没有运行 timestep、Save、Build 或 CUDA。

## 观察与范围

- 当前初态 domain x=[0,0.25]、y=[0,1]，Density 下半区1、上半区2。
- 原生选点已显示 sample11455（i63、j89）、x=0.1240234375、y=0.69921875、Density=2；
  Inspector 仍显示同一 config/build/binary 身份。
- 绘图区分别发送负、正滚轮后，截图坐标轴没有可见变化。
- 普通参数滚动区也未出现可见滚动；该对照不能证明绘图区专有事件处理故障。
- 一次参数区点击明确返回目标错误：point (150, 1020) is over explorer.exe
  FolderView, not target window msrdc.exe。没有盲目重试。
- 按工具恢复规则激活目标并重新观察后，截图从2560×1392变为1516×1037，
  布局恢复为非最大化窗口；刷新后的滚轮仍无可见缩放。
  这是捕获/前台输入关联不稳定的证据，尚不能确定 WSLg、工具或应用中的具体责任。

## 源码核对

PhysicalPlot 的 SVG wheel handler 按事件坐标换算至同一 projection，并检查
inside、deltaY 和 active drag；canvas、axes、marker 与 hit testing 使用同一映射。
容器原生 wheel listener 只 preventDefault，不 stopPropagation；CSS SVG 位于 canvas 上方。
本轮只读核对未发现足以解释全部对照失败的明确源码错误。
不能以此宣布代码正确，也不能以工具返回成功宣布真实 wheel 已送达。

## 正常关闭与下一步

通过原生 Alt+F4 正常关闭，launcher EXIT 0；关闭前记录的9个 owned
Electron/Host/ARCH 进程均按 PID/start 身份复核消失，RT 输入和 binary SHA-256不变。
其他独立 Run 窗口未操作。

原生 zoom/pan/fit 整体仍 **NOT VERIFIED**，全模型桌面出口未完成。
下一步需先取得可靠 wheel event delivery 的证据，再验证缩放后的 axes/data/选点/
Inspector/AMR 映射与 Fit；不以数学单元测试或浏览器合成事件替代原生验收。
不因本轮不确定结果改动科学 Core 或放宽门槛。

原始进程清单/指纹位于本机 ignored studio/.local/integration/native-zoom-20261003/。
