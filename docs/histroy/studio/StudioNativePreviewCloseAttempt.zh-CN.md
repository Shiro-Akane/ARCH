# 原生 Preview 关闭：清理通过，Core 活动边界未确认

2026-10-03，开始源码 e73d226af35a9640f2ed39d0c525895db40e5e36，工作树 clean。

本轮通过独立 Linux Studio 的原生文件选择器打开已有 CellularPreview2D.par，
明确选择真实注册 CellularDet，不根据文件名自动认定模型关联。
该 init-only 样本未声明 tmax，当前工作台报告 Required input has no declared value，
并禁用 Update Preview。未补猜测物理终点、未绕过校验、未改写样本。
这暴露 Preview 专用输入与工作台完整参数检查的范围问题，后续需核对检查用途契约；
本轮不修改 Core 或将其隐藏为通过。

随后原生选择已有 AMR 初始状态对照输入
studio/.local/integration/amr-production-oracle/CellularDet-gbya9tww/CellularDet.par。
保持既有 use_burn=false/tmax=0 和全部参数，未编辑或 Save。
既有页面流程发起 init-only Preview；原生截图显示 Parameters changed · updating、
Cancel Preview 可用及底部 Preview generating，随后直接点击窗口关闭按钮。
不通过 Host mutation API 或进程信号代替原生关闭。

desktop.log：06:25:40.478Z shutdown requested；06:25:40.947Z owned Host exited；
06:25:40.948Z desktop clean shutdown。
只读 /proc 监测记录 Electron53611/start927372、Host53657/start927382、
其直接 ARCH worker54079/start928098；关闭后这些 PID 均不存在。
因此 owned 生命周期清理可确认。

但 monitor 使用0.5秒 HTTP read timeout，248次样本无成功 Preview status 响应。
此前状态读取已观察到 Host elapsed 约1.66秒；这个监测预算不足。
没有捕获本次 request ID、Core stage 或关闭瞬间权威 running 状态。
截图 generating 不能单独证明 Core 当时仍在初始化，而非传输/前端处理。
因此 **Core active Preview close NOT VERIFIED**，不将进程清理结论扩大为此项 PASS。
不自动重放、不用自然完成或其他关闭证据补判本轮。

tracked CellularPreview2D.par 与 CPU binary SHA 均和开始前一致。
无 Run/Restart、Build、Save、CUDA；原始监测仅留
studio/.local/integration/native-active-preview-close，不提交响应数组/科学输出。
未采集科学输出目录前后快照，因此不新增“目录零变化”的证据声明。
只提交精简状态、身份和边界。无需重复未改实现的234项检查。

后续重新核对 live 窗口/Host，以符合实际延迟的 status timeout 捕获活动请求，
再完成 active Preview/AMR close 与文件覆盖等剩余矩阵。
3C 与联合计划仍未整体通过。
