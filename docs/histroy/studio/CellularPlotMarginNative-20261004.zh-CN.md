# CellularDet 二维绘图区边距原生复验 — 2026-10-04

本轮只验证已提交的动态物理绘图区边距，补 Jeans 原生报告中尚缺的二维 colorbar 证据。source fefef8859b4f249053d3755f40232d85efebd5c7，production code b0de7b56；无源码修改，不重复334项回归。

## 输入拒绝与真实来源

初次沿用 full-model-production 的旧 CellularDet.par：当前 schema 报一项 tmax 必需缺失，Preview/initial AMR 按验证 gate 禁用，没有生成数据。窗口47778518正常关闭；保留原输入，不补猜测值、不绕过 gate。

随后从已验收 current-cpu-plotfile-t0 的 CellularDet.par 按字节复制到本轮 ignored 目录，SHA-256 6ca2b63719942d1a900cab8458a4417b0dc88c3a41750e37df5fff55647391e3。该输入 tmax=0 / use_burn=false，是此前批准的初态对照，并不等于原 burn-on 输入的全部配置验证。未执行 simulation 或生成新文件。

managed source 64b0ce2f8d97553f59024978618f1e4848a974e1，CPU ELF d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75，Build ID 92d429da-42d9-4336-b956-79c43440b5c3。tracked inputs匹配；完整 dependency freshness unknown。

## 原生二维结果

真实 Linux 窗口47909590，128×128 / shape=[128,128] / x1-fastest，x=[0,25.6] cm、y=[0,12.8] cm、x3=0，shock_dir=0。startup preparing/current 自行出现，不能声称首次 Preview 是手动按钮请求。

密度全域图和三个 colorbar 刻度 4.408e+7 / 2.704e+7 / 1.000e+7 完整可读。点选 hot-side x=2.9,y=6.35 对应 i14/j63，raw index=63*128+14=8078。

切换 Pressure 为 display-only；选择状态实际重置，不能称为保持。最终压力 colorbar 1.466e+25 / 7.765e+24 / 8.673e+23 完整可读。更长标签使右边距改变；调整实际像素点击后，同一物理位置重新命中8078。七个 Inspector 值保持：rho43177586、P1.4216047e25、T4.667785e9、VELX1.011e9、ENER5.4149083e25、EINT7.4304119e17、VELY0，单位按Core分别显示 g/cm³ / erg/cm³ / K / cm/s / erg/cm³ / erg/g / cm/s。没有改写raw arrays，不作独立EOS oracle。

坐标、热图、样本marker和hit testing使用同一frame；无最终标签裁切。Config仍Saved / configRevision6ca2b6371994 / PreviewCurrent。本轮未做wheel/pan、非方形采样、shock_dir1或AMR新请求，既有证据不被替代或扩大。

## 清理与后续

两个本轮窗口均正常close / launcher exit0；拒绝路径7个owned、成功路径8个owned PID/startTicks均消失。两个config字节不变，managed worktree clean。成功输入指向既有科学output：8个文件前后清单及SHA均保持，没有新建、删除或覆盖；原Plotfile SHA仍8cc5e9e1da007bdc2854a091517a487b08f40976ec9a5580c44f76a1f7d5550b。

原始数据/日志/进程记录留本机.local，本次只提交精简Summary和报告。发现的旧样本缺tmax保留为明确失效输入，不声称全模型旧目录的每份样本都通过当前契约。后续矩阵索引应区分初态、真实运行、native交互与科学验收。

不Build Core/Run/Save/push/tag，不扩展Windows；科学CPU/CUDA/O9未闭合。
