# 全模型 Linux 桌面验收进度：Gaussian 三维代表

2026-10-03。测试源码为 8a0b6cc1a8ed6065be87404363e7a3b4d66d4f8b。
通过既有 Linux arch-studio 入口、production assets 和正式 owned Host 操作独立 WSLg 窗口，没有 Vite 或浏览器替代验收。

## 已直接验证

- Gaussian runtime capability 1/2/3D；当前三维配置选中真实 3D Profile，32×32×32，共32768个 Init samples。
- Density 为该输入的均匀2；切换 Pressure 显示真实空间变化。没有用视觉形状猜测物理错误。
- z固定切片从0到1，坐标由-0.37031250到-0.31093750；随后选择x固定切片，横纵轴正确变为y–z，x=0.2125 cm。
- Host只读前后证据证明 requestId、完整field身份、原始data SHA256和磁盘.par SHA256均未变；没有自动Preview或Save。
- 点击真实sample得到global index1807，indices=[15,24,1]；三坐标及8个原始字段与响应一致。Inspector是Init sample，不是AMR cell value。
- 点击Generate initial AMR后请求成功，complete=true、L0=1；三轴bounds、16×16×16 cellShape及spacing与真实响应逐项一致。
- UI明确显示field/config/build/EOS/native coordinates身份匹配。块边界和cell lines叠加到z及x固定切面，几何Inspector明确提示API未提供AMR cell field arrays。
- 通过窗口关闭按钮正常退出，launcher exit=0；当前Electron/Host PID均消失，生产binary的preview-session/preview-amr进程扫描为空。

## 限制与实际观察

只验收Gaussian Cartesian代表。该输入max_level=0，所以不能声称多层或三维混合细化通过。
切换slice后原sample Inspector清空；源码的slice handlers明确setSelected(null)，当前事实不能被写成“选择跨切片保留”。
继续核对交互要求及保留策略后再决定修改，当前不把它算全阶段通过。
一次y轴菜单点击只关闭列表、未改变x选择，未计为y切片通过。此前滚轮未滚动成功、最大化后坐标选择失败也未计通过；使用重新观察的窗口与真实滚动条完成操作。
完整全模型桌面、曲线坐标、非立方体采样、取消/竞态、混合细化仍待验收。
状态区的hierarchy not constructed属于field Preview的状态快照；独立AMR结果区域显示其完成状态，不能将两条快照合成单一Core响应。

## 身份与数据

Build ID b122233f-61aa-4fd5-932a-a27ded45e302；
binary SHA256 ee3de6cf2b8cd54bc54e70a5c15ffdaa60a9fb39281ae243800f21d1286c2d5a。
完整依赖freshness仍unknown，未提升为clean build。
原始field/AMR响应及配置仅留studio/.local/integration/full-model-production-20261003。
提交只含本报告和精简摘要；没有simulation、科学输出、push、tag或main merge。

## 切片原始样本保留修正与真实复验

复核发现FullModelWorkspaceProgress定义了切面外选择保留，UI已有对应提示；
但slice axis/index两个onChange仍清空selected。仅移除这两处setSelected(null)，
不改请求、配置、Core、数组或AMR逻辑。

重新生成production assets，通过正式Linux独立窗口选择sample783（i15/j24/k0）。
z切片0→1及固定轴z→x之后，Inspector继续显示原坐标与8个原始字段；
切面外警告出现，当前切面marker消失，没有把旧坐标投影成新样本。
只读Host前后对照：requestId、完整identity、shape、data SHA、磁盘配置SHA全部相同。
请求e077293a-2e7d-4c3c-9a41-23d911194e50。
正常窗口关闭exit=0；本轮Electron258997与Host259043已退出。
Studio/Host244/244、lint、typecheck、production build、diff-check通过。
此修正替代上文“清空行为待核查”，保留历史失败观察；完整桌面矩阵尚未完成。
