# Linux 原生滚轮路由诊断

本轮源 060373ea664690b544889af0c33b1af7cd51afec，现有 production 绘图代码 b0de7b56。
受管项目 clean 64b0ce2f，binary/input/窗口/进程身份与逐步输入见同名 Summary。
使用已有 Sod 输入按字节复制，只生成 startup Initial Preview，未运行 simulation 或新 AMR。

## 已观察与尚未证明

原生点击命中 sample 202（x 显示 .39550781），Inspector 的 rho/P/T/VELX/ENER/EINT
为 1/1/2.5/0/2.5/2.5；对应 configRevision 9c8ba5b67718，Preview Current，Saved。
显示设置 disclosure 与 X range 下拉正常响应，保持 Auto，没有修改 Config。

普通窗口两次 plot 内滚轮（-120/-600）、普通内容区对照（+600）和最大化零截图偏移
plot 内滚轮（-120），刷新后均无可见变化。该现象不能区分 WSL/Computer Use 输入转发
与应用 wheel handler。输入前使用当前 screenshotId/returned Window，重新观察后取坐标；
最大化后观察到后续窗口恢复，不能假定每次注入均保持最大化状态。

实际代码 PhysicalPlot.tsx 有 SVG onWheel：非零 deltaY、无 active drag、坐标 inside 后
使用 zoomView；原生 preventDefault 使用相同动态 left/right 边界。只读核对不等于真实事件送达证明。
没有依据修改科学或绘图逻辑，也没有凭数学单测、手动 range 或按钮操作替代 wheel PASS。
Drag/Pan 未执行；Sod/二维/非方形全映射验收仍未闭合。

## 清理与下一步

窗口正常 close，launcher exit0；8 个本轮 PID/startTicks 身份全部消失，
config SHA 不变，受管 output/sod_standard 仍不存在，managed working tree clean。
原始 launcher/profile/log 保留 ignored 本机目录；仅提交处理后诊断，不截取或上传完整桌面。
没有 Core Build、simulation、Save、源码修改或无变化 baseline 重跑；没有 push/tag。

后续需要可证实的 Linux wheel 事件送达路径，再核对物理坐标、marker、hit testing 与
Inspector；本记录只收窄诊断，不将尚未建立的输入证据计为通过。联合 CPU 科学、CUDA、
冻结端点/O9 等完整目标继续按原计划推进。
