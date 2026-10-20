# Plotfile Linux renderer 事件节点（2026-10-05）

工程 PASS；不是科学签收或真实文件人工 UAT。基线 df06683fe1845dc73a4e4b89636e6c27e224a44c。
生产 Viewer、Core 及 root STATUS 无修改。唯一 Linux/WSL 工作区，不开展 Windows 工作。

新增 validation/io/verify_plotfile_renderer_events.mjs；
处理证据 validation/io/results/plotfile-renderer-events-20261005/summary.json。
保存生产组件和验证脚本 SHA-256。

## 实际证据

Electron 44.4.3 / Chromium 152.0.7977.130，Vite 打包实际生产
PlotfileNativeView / PlotfileOverviewView；独立可见 Linux 窗口、软件渲染。
sendInputEvent 实际发送滚轮、鼠标拖动与点击，不以 zoom 按钮替代滚轮。
1D 三格含跳变；2D Cartesian shape=[2,3]、x1-fastest。
native/LOD × 1D/2D × 窗口外尺寸 1280×900/1920×1080，共 8 组 PASS。

必须断言滚轮改变 cell 宽度，拖动实际移动几何、且不触发选择。
点击实际绘制 cell 中心后，native 返回正确 row，LOD 返回对应坐标。
坐标容差 0.015 仅覆盖整数屏幕像素量化，是工程阈值，不是科学预算。
1D pan 保持 field ordinate；2D pan 移动两个空间轴。Fit 恢复原位置。
原始合成 response 序列化不变、fetch 0 次。

## 失败保留

模块路径、argv 解析错误属于前两轮探针问题，自有失败进程已终止。
隐藏窗口初步 PASS 加入“几何必须实际改变”断言后失败，
wheel did not change geometry；该初步 PASS 已撤销。
最终可见窗口 8 组才是本节点结果。
生成资产及失败保留 studio/.local/integration/plotfile-renderer-events*20261005*，不提交。

## 复现及回归

需现有 Linux 图形会话及项目依赖，新输出目录不可存在：

    node validation/io/verify_plotfile_renderer_events.mjs studio/.local/integration/NEW_EVENT_RUN

结束关闭自有窗口，30 秒超时。相关 native-plot-view.test.ts /
plotfile-overview.test.ts 22 项 PASS；node --check 与 git diff --check PASS。
无生产源码改动，不重复已通过的科学 baseline。

## 保留门槛

合成夹具不读取 HDF5，不挂接 Host/Config/Save/Preview，只检查选择 callback，
不声称完成真实 Inspector 取值链、混合 AMR 层级端到端或人工 UAT。
真实文件 reader 证据仍独立，不能相加假定端到端成立。
科学单位/测度/身份、独立数值 oracle、大文件首次扫描成本仍待原计划 review。
RZ/3D/曲线坐标门槛不变；JENS 公共 CUDA 扩展授权和正式长包仍待确认。
