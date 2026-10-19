# RT Linux 原生显示验收与输入限制

## 阶段与身份

联合计划全模型初态/AMR 的 Linux 原生桌面矩阵增量，不是 Plotfile Viewer 验收。
复用已确认存活的独立 UAT launcher session43519，窗口18350414，
ARCH-host-clean-build-20261004。未因此前观察超时重新启动。

受管源码64b0ce2f8d97553f59024978618f1e4848a974e1，真实干净CPU Build
92d429da-42d9-4336-b956-79c43440b5c3，binary SHA
d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75。
Studio启动基线4aea1139；PhysicalPlot最后改动cf743de8。
从启动基线到当前5656f7b8，该组件/desktop main/session代码无变化。
窗口早于最近Plotfile Reader改动，不能作为那些改动的原生UAT。

输入为此前已批准RT2D副本，SHA
aa72d96c240e3cd349fdae1f75bbd79a23a7b144fc926713fb00a0586f890602；
关闭后仍与原参考字节相同，两个源码工作树clean。
本轮只显示操作，没有修改配置/Save、新Preview/AMR、Run/Restart或simulation。

## 实际原生操作

Computer Use捕获独立WSLg窗口，不借用浏览器、脚本DOM或模拟输入事件。
拖动主区域滚动条可上下移动内容并显示整个plot。
数据domain x=[0,.25]cm/y=[0,1]cm，128×128。

点选界面上方原生样本：Inspector index8511，
xIndex63/yIndex66，66*128+63=8511，显示
x=.12402344cm/y=.51953125cm，DENS2g/cm^3、PRES2.4960938erg/cm^3、
VELY-.018645519cm/s。选中标记与界面上方对应，build/config/binary身份保持。
这些数值按UI显示精度记录，不替代独立科学oracle。

选择X Log，含零domain出现明确正范围提示，
plot停绘且Inspector保留上述raw值，无abs/epsilon修补。
点击Fit恢复X/Y Linear、Auto、density图及同一选择。
仅证明invalid Log恢复；没有据此宣称wheel zoom后Fit验收。

## 未关闭的滚轮/pan

图区域聚焦后scrollY=-600仍未观察到轴缩放。
普通左参数滚动区聚焦后scrollY=600同样未观察到滚动。
而鼠标click与scrollbar drag都有效。
新增普通滚动区对照进一步限定：目前不能区分wheel投递限制与应用问题。
不据此修改投影/物理数学，不伪造native zoom/pan PASS。
zoom/pan仍待可靠真实滚轮或人工步骤验证；Fit的本次通过只覆盖上段路径。

此前原用户旧窗口未在当前inventory中出现；本次未操作其他窗口。
不能由本次记录推断旧窗口在更早启动/关闭过程中完全未受影响。

## 关闭与进程证据

关闭前按本轮独立user-data目录识别Electron及其子进程，
记录PID+startTicks。包括Host1508143、ARCH preview-session1509481。
通过窗口X正常关闭，launcher session43519 exit0。
全部8个owned进程原PID/startTicks不再存在，窗口inventory亦已消失；
没有kill未知进程或操作其余ARCH独立terminal窗口。
完整owned列表、input/source/binary身份及范围见同名Summary.json。
局部日志留本机ignored路径，未提交原始图像/H5/checkpoint。

清单：本次完成原生scrollbar可达性、Inspector、Log拒绝、限定Fit恢复、
新clean CPU binary的owned关闭清理；native wheel zoom/pan仍未完成。
完整模型矩阵、O7科学待决、CUDA/O9及联合目标继续未完成。
不重复自动回归：本轮无源码修改，无新失败证据要求重跑已过检查。
