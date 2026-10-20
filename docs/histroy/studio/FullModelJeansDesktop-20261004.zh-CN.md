# JeansWave 原生矩阵与刻度 finding — 2026-10-04

按联合计划推进全模型初始视图；不是 Jeans 演化、JENS 或 Poisson 科学验收。沿用既有完整 CGS 输入，未修改科学 Core、输入、floor、容差或预算。没有 Build Core、Run、Save、checkpoint 或 simulation output。

## 原生证据

初次窗口 20382030，source b2a9f7c6ac2cc0eeca4eb62c14435a37753dc3f0 / production code 584508ea。managed source 64b0ce2f8d97553f59024978618f1e4848a974e1，ELF d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75，Build ID 92d429da-42d9-4336-b956-79c43440b5c3。tracked inputs 匹配；完整 dependency freshness unknown。

512 Init samples、x=[0,1] cm，密度约 [9.99900e6,1.00010e7] g/cm³。sample 255 / x=.4990234375 cm：rho=9999000.4、P=5999000.4 erg/cm³、T=1.2533984e-7 K、VELX=-.000088706733 cm/s、ENER=8998500.7 erg/cm³、EINT=.89994002 erg/g。按当前 authoritative Init 的根网格 sinc(k*dx/2) 修正计算 rho=9999000.42036401；这是同公式算术交叉检查，不是独立物理参考或演化预算。

单次 Generate initial AMR：Complete、4 leaf / L0=4、0 passes、max_blocks=32 / working capacity=32、64 active cells、base/with-species bytes=18,432、pool=147,456、species=0。field/config/build/EOS/native coordinate identity matching。未执行 block Inspector；没有 AMR cell field arrays，Init samples 不冒充 AMR cell values。resource estimate 不作 OOM 预测。startup preparing/current 自行出现，不记成手动首次生成。

## finding 与修改

旧 formatter 的固定有效位数将多个刻度合并为 1.000e+7 / 9.999e+6。66f4f6f110ccb78134cc293cb80772f2ceac9e63 新增 formatPlotTicks：仅文本碰撞时逐步增加精度，保留 signed zero、负值、原数据及坐标投影。

中间窗口 48042634 发现更长标签被固定左边距裁切。b0de7b567cd4224eca42557bde14819f342ed031 根据纵轴和 colorbar 标签长度留边距，canvas/SVG/交互共享 frame；wheel prevent 区域同步该 frame。没有修改 scientific arrays、Config、Inspector 格式或 Core 请求。

最终 source b0de7b567cd4224eca42557bde14819f342ed031 的真实 Linux production 窗口 5312346：六个刻度从上到下 1.00011e+7、1.00007e+7、1.00002e+7、9.99978e+6、9.99934e+6、9.99890e+6，全部可见、不重复、未裁切。点击中心仍 sample255，六个 Inspector 值和 configRevision 636c7a948009 与此前一致。没有 native wheel/pan 验收声明；二维 colorbar 边距的完整原生复验仍应在后续相关场景检查。

## 检查与清理

新增三个显示反例：Jeans 大背景小扰动、相邻 FP64/signed-zero、带符号小量及 log ticks。相关 12/12、边距改动后全 334/334、skip0。首次 margin lint 报 left effect dependency 缺失，原日志保留；补齐后 lint/typecheck/production build/diff check PASS。仅依赖数组修正后没有重复全套，最终新 production native UAT 通过。Vite 既有 chunk-size warning 保留，不作为科学失败或顺手拆包理由。

三个本轮窗口均正常 close / launcher exit0；每轮8个 owned PID/startTicks 已消失。三个 config SHA 一致且不变，output/jeans_wave 不存在，managed worktree clean。记录在 .local/integration/native-jeans-* 和 jeans-tick-precision；仅提交处理后摘要，不提交原始数组、日志、H5 或截图。

Linux/WSL 范围，无 Windows 适配、push 或 tag。联合科学 CPU / CUDA / O9 尚未完成。
