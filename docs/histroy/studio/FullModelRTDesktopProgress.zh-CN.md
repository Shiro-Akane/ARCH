# RT 二维初态原生桌面验收

基线 f6a67758549ab4045ebcfa918648fcac9423acdf。Linux/WSL production
独立 ARCH Studio 窗口，使用已有本地 RT.par。未改 scientific Core、未重编 ARCH、
未进入 timestep、未生成新的科学 output。配置中的 tmax 不表示本轮运行了演化。

## Field 与 Inspector

Cartesian x=[0,0.25]、y=[0,1]，shape=[128,128]、x1-fastest，
16384 初态样本。热图上部密度2、下部1、分界y=0.5；单位来自 Core。
此前一次 pane 滚动尚未稳定时的点选不作为屏幕坐标映射验收证据。
在稳定窗口重新点选上部，Inspector 显示 raw index13491、indices=[51,105]、
坐标[0.1005859375,0.82421875]。Density=2，Pressure=2.43515625，
Temperature=0.004242432491289199；七个 raw fields 对照实际 response 一致。
只验证该初态响应与显示链路，不判断 RT 演化物理正确性。

## Limited AMR 与显示操作

通过原生 UI 将 Preview block budget 设为64（内存128MiB保持），不是修改
.par 中 max_blocks=768。Generate Initial AMR 返回 leaf64、全部level0、
completedPasses0、configuredMaxLevel2、complete=false；
UI 明确 Limited/Incomplete、regrid-exceeds-working-capacity，
没有将最后完整平衡的 root hierarchy 冒称细化完成。

原生 AMR block Inspector 选择0:1:3:0，显示 level0、
logicalIndex=[1,3,0]、bounds=[0.0625,0.1875]..[0.125,0.25]、
cellShape=[16,16]、cellSpacing=[0.00390625,0.00390625]cm。
API 未提供 AMR cell field arrays；普通 Init sample 的七字段不称为 AMR cell value。
显示资源规模估计，并明确其不等于 OOM预测。
关闭 cell lines 后仍显示 root outlines、所选 block 与普通样本 marker。

这些显示/选择操作前后 field/AMR requestId、project/case/config/build/binary
身份、完整数据 digest、mesh digest 和 disk config digest 逐项不变。
没有重新执行 Core Preview/AMR，没有编辑/Save配置。

一次 Computer Use drag 仅改变样本选择、轴范围未观察到变化。后续审查确认
panView 把范围限制在完整 domain 内；完整 domain 尚未 zoom 时拖动不能改变轴，
因此这次观察不构成平移故障证据。代码路径另要求连续 pointer-move。

再次启动 production RT，先点选聚焦绘图区，再尝试 scrollY=-240，
未观察到缩放轴变化。这是原生操作未能完成的记录；尚未区分输入投递与应用问题，
不据此修改数学或静默换成其他输入验收，也不记 native zoom/pan PASS。
本轮窗口正常关闭exit0。新增边界回归确认全domain平移固定、zoom后边界钳位、
原值与投影逆映射保持；它不能替代原生鼠标交互。
界面提示澄清“drag a zoomed view to pan”，未改变交互或科学行为。
Fit 后映射也尚未专项原生验收。

## 生命周期与覆盖边界

关闭前实际记录 Electron276655、Host276701、owned preview-session277126
(parent276701)。原生窗口正常关闭，前台 launcher session 返回exit0。
关闭后三个 /proc 实体均不存在；所选 production binary 的 preview-session/
preview-amr 进程扫描为空。不是仅凭窗口消失判定清理通过。

完整 raw response 留在 ignored studio/.local/integration/
full-model-production-20261003/native-rt-*，提交仅含指标与身份摘要。
binary/build 身份见 Summary.json；完整依赖 freshness 仍 unknown。
本步只补 RT 初态显示、limited AMR、身份保持、关闭清理的桌面证据；
不是全部模型矩阵、native zoom/pan、RT长轨迹、CUDA或科学验收完成。
没有 push/tag/main merge，没有 Windows适配。
