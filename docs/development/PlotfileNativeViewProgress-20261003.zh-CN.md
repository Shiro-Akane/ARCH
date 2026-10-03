# 原生 Plotfile 单块区域显示进度（2026-10-03）

对应联合计划 3C 之后独立 plt 排期；整体 O7/O9 目标未完成。本次将原生查询接入可缩放图形视图，是首版全域 Viewer 的一个实现增量，不能称为全域总览或完整 LOD。

## 实现

- Project Plotfile audit 请求成功后，candidate NativeGrid 数据显示 1D 原生单元线段或 2D 原生单元矩形。bounds 直接来自 H5，不从存储中心猜单元尺寸。
- 共用 physical range/fraction 映射绘制、缩放、平移和拾取；SVG 使用 x1 向右、x2 向上。1D 纵轴是存储场值，2D 纵轴是 x2，色值独立消费场值。
- 2D 使用现有 Viridis，并显示 raw color range。单元轮廓是真实读取区域的 bounds；只显示其文件内 logical key/level，不宣传成完整 AMR hierarchy。
- 表格选择、图上点击与键盘 Enter/Space 均复用 selectedRow，Inspector 保持同一文件 digest、block/global index、raw value。
- Zoom in/out 按钮、非 passive 滚轮监听、pointer capture 平移、Fit slice；cancel 恢复拖动前视口。成功替换 digest/field/block/start/shape 重置视口，失败/取消保留此前数据和身份。
- 显示变换没有 Host 调用、Config 修改、Save 或 Preview。输入数组不变；非有限值在 2D 显示 magenta，1D 保留缺口，原始字符串仍可从表格/Inspector 查询。
- 单块区域仍有 512 样本和现有响应上限；legacy/未记录 NativeGrid 的文件保持表格，不自行推断 native bounds。候选状态、未知单位与科学审核待定仍明确展示。

## 验证

新增非方形 3×2 x1-fastest 网格测试，检查各原生单元在 zoom/pan 前后通过同一物理映射拾取；检查 zoom anchor、逆向平移、无效视口、1D raw range 与无效值保留。

npm test 286 PASS / 0 fail；lint、typecheck、production build PASS。仅当前 UI 相关回归，没有重编科学 Core。
生产构建仍有现存大 chunk 提示，本次未扩大为打包优化。

真实既有 t=0 H5 → bounded reader → 客户端严格校验 → React SVG SSR：
- Sod：block 11，16 个单元，响应 3572 bytes。
- CellularDet：block 19，16×16 共 256 个单元，响应 33098 bytes。
- 图中可选择元素数量与原生单元数量一致，已选样本标识存在，2D raw color range 存在。读取前后 H5 字节完全一致。

SSR 与纯映射测试不代替 Linux 独立窗口交互 UAT。没有用截图或 renderer 事件冒充物理鼠标/窗口验收。

## 尚未完成

全域总览、跨块 LOD/索引/有界缓存、可见原生 AMR hierarchy、视口查询、首域扫描成本与大型文件取消/资源证据、真实非方形全域文件验证、单位/科学身份 review 和 Linux 桌面真实 UAT。现有 audit renderEligible=false 保留，候选 slice view 不宣称为正式科学 Viewer。

固定像素尺寸只约束返回量；首次全域总览仍可能扫描大量叶块。成本基线见 PlotfileQueryCostProgress-20261003.zh-CN.md。

原始 H5 和 HTML/日志均保留在本机 studio/.local/integration/；提交仅实现、测试及处理后的摘要。无 simulation、无 CUDA、无 Windows 适配。
