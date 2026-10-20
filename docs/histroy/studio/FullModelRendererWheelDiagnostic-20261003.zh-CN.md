# Production renderer 缩放、平移与选点诊断

基线 8557b900881278308d43086cdcb6cbea500a8e3b。使用现有 CPU binary 和 RT128²初态，
正式 desktop/main.cjs、preload 和 production dist，临时诊断脚本仅存本机 ignored 目录。
这是一项 **renderer integration test**，不是 Computer Use / WSLg 原生用户验收。

## 实际事件证据

Electron webContents.sendInputEvent 的 mouseWheel(+120) 产生 DOM deltaY=-120，
capture listener 确认目标为 svg；物理坐标轴从 x=[0,.25]、y=[0,1]
缩至 x=[.025343,.22534]、y=[.090854,.89085]。
反向 Electron wheel 产生 DOM +120，完整坐标域恢复。
独立 DOM wheel(-120) 同样缩放，说明 React handler 实际执行，
不是仅计算 zoomView helper 的数学回归。

在缩放状态下 mouseDown→mouseMove(+60,-20)→mouseUp，
视域变为 x=[.0088599,.20886]、y=[.048665,.84866]。
随后真实 renderer pointer 点击显示 selected sample circle；
Fit 恢复初始坐标域并重投影同一选点 marker。
输出 JSON 逐阶段检查 axes、marker、Current 和 dirty 状态。
未覆盖 AMR、Sod x_pos、全部几何或原始 Inspector 字段，因此不推广为全模型桌面出口。

## Display-only 与生命周期

记录测试阶段 fetch 的 path/method，全部是 GET configure/status、
preview/status、build/status 和 runs；没有 Preview/Save/Build/Run mutation request。
上一版仅以 URL 包含 preview 计数，混入 status 轮询；
本版纠正测量口径，原始首版结果保留而不作为 auto-Preview 证据。
各阶段 Preview Current、dirty=false，磁盘 config 与 binary SHA 均未变。
未新增 timestep、scientific output 或 CUDA。

launcher 正常退出0，desktop.log 记录 owned Host exited 与 clean shutdown；
四次尝试记录的 desktop/Host PID 均不存在。没有操作其他独立 Run。
初次 harness 绝对 require 导入错误及第二次 executeJavaScript 不可克隆返回值
均保留在本机诊断记录；正式源码未修改，不把失败抹去或计为通过。

## 结论与下一步

production renderer 的 zoom/pan/selection marker/Fit 在本次二维 RT 范围通过。
结合原生诊断的目标窗口错配，下一步应核对 WSLg 捕获/输入关联，
而不是凭无可见缩放改写 PhysicalPlot。
**原生 WSLg 滚轮仍 NOT VERIFIED**，不能用本测试替代真实交互验收。
完整联合计划仍进行中；没有 push/tag/main merge。

原始脚本、逐阶段 JSON 和日志保存在
studio/.local/integration/renderer-wheel-20261003/；提交仅处理后的摘要。
