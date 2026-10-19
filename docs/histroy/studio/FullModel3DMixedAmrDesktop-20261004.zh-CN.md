# Linux 三维混合 Initial AMR 原生桌面复验

日期：2026-10-04。基线 3565424972716a59b5c2feb9803c569032ddaea6。
本轮为已实现全模型初始化功能的桌面验收补证，Plotfile 首版范围仍限 Sod 1D 与 Cartesian CellularDet 2D。
没有运行 simulation、修改科学 Core、CUDA 验收、push 或 tag。

## 环境与真实构建身份

实际操作 ARCH Studio — ARCH-compute-optim 的 Linux/WSLg 独立窗口，正式 Node Host 与 CPU Core。
通过 native UI 执行固定 studio-cpu-release Build，12 个增量步骤，23.572 秒；
未主动 configure。Build source HEAD 为本轮基线，repositoryDirty=false。
Build ID：851a260f-4bb3-4287-8307-5e92a789e117。
binary SHA：2c53420cde6f1e01c7997b39999d6073f5496e0fe3278e84b36e976de6eea117。
tracked/configuration inputs 在 Build 前后稳定；compilerInputsStableDuringBuild=false。
UI 如实保留完整依赖 freshness unknown，不能据此声称全部依赖认证。

原生文件选择器载入 src/api/examples/local-workflow/rt-3d-limited-amr.par；
选择真实 registry 的 RT。初次 Ctrl+L/Return 路径输入未确认成功，后来使用实际 Open 按钮确认加载，
不将未确认输入记为通过，也不据此认定产品根因。
配置 SHA 339289de3d8d979c985d5a25c0a2c1fb5c3c3b1e7bde82ad3e52a296e35966b1，操作后未改变。

## 操作与结果

RT 32×32×32 初始场返回 Current；DENS 范围 1–2 g/cm^3。
固定 z/x/y 的第 0 个样本分别显示 x-y/y-z/x-z；
固定坐标分别为 z=.00390625 cm、x=.00390625 cm、y=.015625 cm。
x-y/y-z 面的密度跳变与 y=.5 对应；y 低端的 x-z 面为常量密度。
这些是原生窗口实际显示证据，不是截图注入或手工生成数组。

AMR 独立预算预先设为 512 blocks / 256 MiB，与既有混合层级夹具一致。
返回最后完成的 balanced hierarchy：
10 leaves，L0=2 / L1=8 / L2=0；completedPasses=1；
workingCapacity=18，reason=regrid-exceeds-working-capacity。
界面明确 Limited / Incomplete，资源估计明确不是 OOM prediction。
没有因 limited 再提高预算。

图上命中右侧 L1 中间块 1:1:2:0：
bounds x=[.125,.25]、y=[1/3,.5]、z=[0,.125] cm；
cellShape=16³，spacing=[.0078125,1/96,.0078125] cm。
Inspector 显示原始 logicalIndex/level/bounds，声明 API 没有 AMR cell field arrays，
未把 Init 采样颜色称为 AMR cell average。
隐藏 L1 后，其 outlines 消失，原区域点击不能重新命中 L1。

## Finding 与最小修复

旧 AmrControls 的 dropdown 过滤 hidden levels，而 Inspector 从完整 leaves 取旧 selected key；
因此 hidden L1 后仍显示旧几何。补齐相同 level filter，并使用可见 leaf 的 key 作为 select value。
没有修改 selection 的原始身份、网格、场值、布局求值或 Core；重新显示层级可恢复原选择。

npm test 321/321、lint、typecheck、production build、git diff --check 均通过。
production index-JbL_fzAw.js 在同一 Linux 窗口加载后，重新取得真实 RT 初始场与同预算 limited AMR，
选 1:1:2:0 → hide L1 → Inspector 收起且 dropdown placeholder → show L1 → 同 key/bounds 恢复。
这是受新 UI 改动影响的复验；没有重复 CPU Build。
全过程 Working Copy Saved、disk in-sync；未 Save、Run、Restart。

## 未覆盖与保留事项

- native wheel zoom/pan 仍未计通过；当前输入捕获限制不能升级为产品已通过。
- field Preview 的状态快照说明 hierarchy not constructed，与独立 limited AMR 结果是两个身份；
  其标题文字仍可能误读，应另做明确的显示区分 review，不能合并或伪造 Core 状态。
- display-only 切片来自 UI 和代码路径证据，本轮未提取现有 Host 凭证或抓取完整响应数组；
  不以截图替代位级数组、请求 identity 比较。
- 本轮不是 Plotfile 3D 支持、完整科学验收、RZ/JENS 生产接线或平台性能验收。
- 原始数据、Build/check 日志留 studio/.local/integration；提交仅精简 Summary 与报告。
