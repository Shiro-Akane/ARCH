# Linux 原生显式 Zoom 控件与 pan 验证边界

基线 2d340a3edb087e2e436236368d18c297f46471bd，加 PhysicalPlot 的单文件 UI patch。
production asset index-B9q5BL2w.js；精确输入、binary 和源文件 SHA 见 Summary。
Linux/WSL 原生 Electron 窗口，独立 ignored Sod 副本，既有 CPU binary；未 Build Core。

## 最小实现

PhysicalPlot 在既有 Fit 前增加 Zoom in / Zoom out，中心锚点和 .8/1.25 因子
复用原 zoomView，与 wheel 共用单一状态更新入口。错误 geometry 时按钮 disabled，
drag 中不缩放。没有新增坐标公式、科学默认、Config/Save/Preview callback。
现有 Fit、raw arrays、Inspector 与 marker 仍使用原同一 projection。
这为桌面和键盘提供显式缩放入口，不宣称修复了 WSLg wheel routing。

## 原生实际观察

首次旧 asset 窗口只观察设置区；正常关闭 exit0，8 owned 进程全部退出。
重新启动新 production asset 后观察 startup automatic init-only Preview Preparing→Current；
不是手动 Update Preview，也不能记为 zero Preview。

最大化真实 2560×1392：
- Zoom in：x=[0,1]→[.1,.9]，field-y=[.08125,1.04375]→[.1775,.9475]。
- 点击(1000,800)：sample188，x=.3681640625，六个 Inspector 显示值=[1,1,2.5,0,2.5,2.5]（显示精度，不是FP64位模式比较）。
  与当前 x frame [400,2190] 的独立线性算术落入同一512采样 bin。
- sky drag (1000,800)→(1200,800) 后，range 与 x_pos marker 未平移，
  Inspector 变为终点 sample234/x=.4580078125。再次 capture 结果相同。
  因此 pan NOT VERIFIED；不能称 selected sample retention 或 pointer-pan PASS。
  目前不区分输入转发/Computer Use 与应用 handler，也不做无证据修改。
- Zoom out 恢复 x=[0,1] 和原 field-y；sample234/Inspector显示值保持，选中样本圆环重新可见。
  x_pos=.5 在 discontinuity 上，Config Saved/Preview Current 及身份前缀始终保持。
- 一维同时缩放 field-y 后，两个 plateau 超出当前显示范围；不改变原值，
  圆环被 clip 而不是错误重定位。Zoom out 后恢复可见。

本轮只证明 button zoom 的真实入口与上述 1D hit/恢复；
wheel、drag pan、2D 原生控件和全部 AMR overlay 映射不据此宣称已验收。

## 检查与清理

334/334 npm test、lint、build PASS；build 内 tsc --noEmit PASS，不重复运行。
695 modules；既有 >500kB bundle warning 保留。diff check PASS。
新窗口 normal close exit0；8个owned PID/startTicks全部消失。
输入与 binary SHA 不变，受管 worktree clean，配置指定 output/sod_standard 不存在。
未点击 Update Preview、Save、Run 或 AMR；未生成 scientific output。
完整日志/输入仅本机 ignored；提交源码与处理后摘要，不提交 dist/node_modules/原始数组。
不 push/tag，不改 architecture rules；O7科学待决、CPU/CUDA/O9和整体目标未完成。
