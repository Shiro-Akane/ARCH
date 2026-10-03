# ExternalGravity Linux 原生初态与 AMR 验证 — 2026-10-04

本轮仅补全模型原生矩阵的一行，不是外部重力演化验收。保留既有合法输入：Cartesian 1D、gravity_type=external、三个 acceleration 均为 0、rho0=1、pressure0=1、velocity_x0=0；未编辑、Save、Build 或 Run。

## 身份与数据

启动 source e9a37026212b408914b0490463f30db0e3bdddfe；production code 584508ea1d9fc23009da69973de7b13abf2fc00e。managed source 64b0ce2f8d97553f59024978618f1e4848a974e1、CPU ELF SHA-256 d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75、Build ID 92d429da-42d9-4336-b956-79c43440b5c3。tracked inputs 匹配成功 Build；完整 dependency freshness 仍 unknown。

真实 Linux 独立窗口 41813672：ExternalGravity Registered / field supported / initial AMR supported。512 Init samples，x=[0,1] cm，密度均为 1 g/cm³。点击 sample 255（x=.4990234375 cm）后 Inspector 显示 rho=1、P=1 erg/cm³、T=3.4818942e-7 K、VELX=0 cm/s、ENER=2.5 erg/cm³、EINT=2.5 erg/g。这是显示的 Core 初态值，不是独立 EOS oracle。

Gravity 区域可达，external 值和 X acceleration=0 cm/s² 可见；说明明确配置不等于 gravity field Preview。本轮未完整查看 Y/Z 控件，只确认输入文件未变。初始化检查到 Current，界面报告三个 raw probes，但未展开核对，不能作为其逐值验收。启动时自行出现 preparing/current，不能声称首次 Preview 由手动按钮产生。

## 初始 AMR

只点击一次 Generate initial AMR：Complete、8 leaf blocks、L0=8、0 passes、configured max_blocks=32 / working capacity=32。128 active cells；level base / with-species bytes 均 36,864，pool base 147,456，species count=0。资源估计不是 OOM 预测。

列表选择 logicalKey 0:0:0:0：level 0，logicalIndex=(0,0,0)，x bounds=[0,.125] cm、cellShape=16、cellSpacing=.0078125 cm，与 .125/16 的几何算术一致。UI 显示 field/config/build/EOS/native coordinates 身份匹配。AMR API 无 cell field arrays；右侧 Init sample 值不能称为 AMR cell value。

## 清理与边界

窗口正常关闭，launcher exit 0；8 个事先记录的 owned PID/startTicks 均已消失。config SHA-256 73f9a423fc378a1ac063d4190e022122c8db460298d524539ad958d19ee30a7d 未变，配置所指 output/sod_standard 不存在，managed worktree clean。仅原始日志与进程记录留本机 ignored .local，提交处理后的摘要。

没有修改源码，不重复已通过的 331 项自动基线，没有 push/tag 或 Windows 适配。没有非零外力、Poisson、simulation、checkpoint/evolution 验收；科学 CPU/CUDA/O9 与全模型矩阵剩余项仍未闭合。
