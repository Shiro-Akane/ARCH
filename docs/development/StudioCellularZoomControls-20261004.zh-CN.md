# CellularDet 二维显式 Zoom 原生复验
本次 production Linux Electron 使用 b0af53a8543d327fdc499e9d71aec00d0cc0a89d。
复验共享 PhysicalPlot 控件，不将 Init sample 称为 Plotfile 原生 AMR 单元值。
现有已批准 t=0 / use_burn=false 输入按字节复制；启动自动 init-only Preview。
未点击 Update Preview、AMR、Run、Build 或 Save；没有科学演化。

128×128、shape=[128,128]、x1-fastest，domain x=[0,25.6] / y=[0,12.8] cm、x3=0。
这属于非正方形物理 domain，不是 Nx!=Ny 采样验收。
Zoom in 后 x=[2.56,23.04] / y=[1.28,11.52]，heatmap 分界同步。
三个 density colorbar 标签保持 1.000e7 / 2.704e7 / 4.408e7。
在缩放图中点击后得到 i=24 / j=62 / raw index=7960=62*128+24，
中心 x=4.9 / y=6.25 cm，与独立坐标算术一致。
Zoom out 与再次 Zoom in→Fit 均恢复完整 domain；同一样本、Inspector 显示值、
config/build/binary 身份保留。Density 显示 43093166 g/cm^3；
显示小数一致不是原始 FP64 位模式验收。

Config 始终 Saved / disk in-sync，Preview Current。既有科学 output 的8文件集合及 SHA
完全不变；输入与 binary SHA 不变。正常关窗 launcher exit0，8个本轮 owned PID/startTicks
均消失，managed worktree clean。具体身份与检查见同名 Summary。
raw launch/stdout/stderr/cleanup 留在本机 ignored .local，不提交原始 H5 或 checkpoint。

本轮无源码修改，不重复上一提交的334/334、lint、tsc/production build。
Native wheel/pan 仍 NOT VERIFIED；没有用 Zoom/Fit 代替其验收。
完整依赖 freshness、独立科学 oracle、二维演化与 CPU/CUDA/O9 不因此通过。
