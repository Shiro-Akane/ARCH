# Linux 球坐标与非立方体初始预览桌面进度

2026-10-03；源码865f49bfb420d29568f1950d803e2c6f270d8177。
既有arch-studio Linux独立窗口、production assets与正式Host/Core；没有Vite、Mock或simulation。

## 真实操作与证据

沿维护的FullInitialPreview.gaussian spherical输入准备本地副本。
在界面采样控件分别输入Nx=5、Ny=3、Nz=2。sampling设置按既有工作流更新真实Preview，
不改.par；不能把sampling变化写成display-only切片操作。
真实Host最终shape=[2,3,5]、count=30、x1-fastest；Core轴单位cm/rad/rad。
字段选择器显示Native r/theta/phi velocity，未误称Cartesian速度分量。
Pressure切面显示真实非均匀值。

phi固定切面为5×3，轴r cm与theta rad。
热图点击得到global index12，i=2/j=2/k=0；原生坐标0.6 cm、2.1333333333333333 rad、0.075 rad。
八字段Inspector逐项与真实原数组一致，精简标量见摘要。
选择r固定切面后展示theta/phi 3×2，横纵单位均rad；
原index12位于切面外，原Inspector保留并提示，不绘制错误marker。

点击Generate initial AMR得到succeeded/complete=true，三维球坐标root leaf=1。
AMR cell lines在phi和r切面可见，字段与mesh的project/case/config/build/binary匹配，
坐标metadata/EOS符合既有validator；没有把Init样本当AMR cell value。
字段切换/固定轴显示操作前后，requestId、完整身份、原数组SHA与磁盘配置SHA相同。
已有root输入不证明三维混合细化，也不证明RZ或新引力数学。

窗口通过close按钮正常退出，launcher exit0，当前Electron261436/Host261482均不存在。
原始field/AMR arrays、日志和.par留studio/.local/integration/full-model-production-20261003，
提交仅精简结果。没有Core修改、编译、演化、push或tag。

## 待完成

一次wheel zoom输入未产生可见范围变化，未计通过，也不根据单次Computer Use输入判定产品根因。
完整桌面矩阵、native zoom/pan/fit直接验证、取消/竞态、uniform-state桌面仍待完成。
Cartesian与spherical代表不能替代所有模型/几何组合验收。
plt、JENS、RZ、CUDA及批准长轨迹仍按联合计划继续。
仅新增证据文档，无源码变化，不重复刚通过的244项检查。
