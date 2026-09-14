# 扩展耦合规模发现的共享组分缺陷

本目录记录共享组分修复及其科学验收：**原 15 类回归和新增 18/18 组耦合规模对照全部通过**。
这不是 S5 正式性能或 150/200 核素完整应用验收；这两项仍单独继续。
不是通过降低精度或停用 AMR 让旧失败消失；旧输入、完整失败、消融和修复后的诊断均保留。

## 原失败及定位

将既有 `coupled_bd_rkl2_all_transport` 扩为 128 初始块，保持 BurnGradient、Helm、aprox13、
BD/DenseLU、三种输运、动态 ENUC AMR、终止时间 1e-10 和原 ODE 容差。
原 fused 基线与 S5 的 CPU 在第 60 步出现 `min(X)=-2.6781185417235678e-19`，
S5 CUDA 在第 63 步后第二段燃烧失败；原 `smallx=1e-20` 与 `-10*smallx` 拒绝门槛未改。

70 步局部消融显示：停用组分扩散／全部扩散／改用 RKL1 仍失败；仅停用 AMR 可以完成这段定位。
这些是有声明的定位实验，不是科学验收。记录到 Hydro reflux 将一个粗层单元核素 12 从
`1.0014665223958527e-20` 改为上述负值；后续扩散保留而非产生该负值。

## 两个独立缺陷与修复

1. coarse→fine 闭合固定使用最后核素。对 `(0.3,0.6,0.1,1e-20)`，旧 `rho-sum(other rhoX)`
   可产生 `1.1102230246251565e-16` 的虚假痕量，随后细层面通量可抽空粗单元真实痕量。
   共享叶函数改在父单元主导核素闭合，所有兄弟用同一个选择，平局固定首 index；
   痕量独立重构，不 clipping。真正不满足可行性的兄弟族仍整体 constant fallback。
2. MUSCL 各组分独立 MC 限幅不保持面上的和为 1。独立四单元反例给出左面原始和 0.975。
   将 PPM 已有的归一化操作移到同一共享 helper，PPM 保持原操作及顺序，
   Host/CUDA MUSCL 都调用它，使组分通量与质量通量保持一致。

只修第一项，诊断版可越过原失败点，却在第 123 步 BD 步长降至 1e-22 以下而失败。
日志显示线性求解成功、截断误差为 0，状态温度约 2.39e9；不是低温 floor 问题。
输入组分和漂移后，ODE 的既有归一化造成不随 H 缩小的能量变化，使能量闭合拒绝。
因此没有修改 BD、线性求解器、能量门槛或步长下限，而是修复上游面组分闭合。

独立 C++ 反例使用真实旧／新头文件：v2 中旧版两项均失败，修复后两项均通过。
最早 v1 的非均匀痕量 stencil 触发了旧 constant fallback，没有复现该缺陷；其记录亦保留，
不将其计为有效 red/green 证据。

## 当前验证边界

两修复合用的 **带日志 CPU 诊断版** 完整到 1e-10：706 步、158 最终 AMR 块。
独立 checkpoint 检查的组分、质量、电荷及含核反应源能量原预算均通过。
当时另有编译；568.78 秒不是正式计时，也不是 CUDA 结果。
其最初 `record.json.scope` 字符串来自旧 logging-only 脚本，已在独立 qualification 文件中
明确更正：该次实际使用两项数学补丁加日志，原 JSON 不作事后篡改。

新增测试覆盖 7 个组分 stencil × 1D/2D/3D、24 个无 scatter 负例、真实 fallback、
零／1e-20 痕量、稳定主导选择，以及 Host/CUDA MUSCL 的独立预期值。
生产 CUDA archive／ARCH 已完成完整构建。关键 128 块 CPU8/GPU8 对照均为 706 步、
158 最终块、完整到 1e-10，字段与步数／regrid 工作量比较通过；其原 1e-12 源平衡预算下：

| 误差 | CPU | CUDA |
|---|---:|---:|
| 含核反应源的能量相对误差 | 8.2981e-14 | 8.1950e-14 |
| 质量相对误差 | 2.0489e-15 | 2.0489e-15 |
| 电荷绝对误差 | 1.0300e-15 | 1.0300e-15 |
| 单点组分和最大绝对误差 | 2.2205e-16 | 3.3307e-16 |

见 `critical-b128-evidence.json`；其计时与 CPU 编译并发，不用于加速比。
原 15 类科学回归已全部通过，包括曲线坐标、长期 AMR 和两类耦合跨后端 restart。
BD／BE_NR／ROS4 × RKL1／RKL2 × 8／32／128 初始块的 18 组全输运耦合矩阵也已全部通过：
36 次 CPU8／CUDA8 运行、18 次字段与宏步／逐步 regrid 工作量比较，均到原定终止时间 1e-10。
此处不声称逐燃烧单元内部 ODE 的尝试／拒绝次数完全相同；当前全应用日志不提供这些逐单元计数。
这 36 次运行中，最大含核反应源能量相对误差为 `8.8204e-14`，质量相对误差 `2.0489e-15`，
电荷绝对误差 `1.1055e-15`，单点组分和误差 `4.4409e-16`；原门槛仍为 1e-12。
完整逐组结果见 `summary.json`，原始资格和逐步 regrid 记录见 `science/details/`。
性能对照双方都应用相同数学修复；失败的旧 128 块结果不能作速度分母。

执行清单补充：继承的 S5 显式构建清单漏列 standalone `arch_cuda_amr_composition`，
第一次合同检查实际用了先前 18 组测试；该日志不计为 21 组覆盖。
已显式补编该目标，并逐项核对前后应用、源码、CMakeCache 和 compile_commands 身份不变。
随后的合同输出已确认为 `valid=21 invalid_no_scatter=24` 且通过。
另一次独立 clean leaf 构建的两个 Host/CUDA 测试也已真实执行通过，输出同样为 21 有效组
及 24 个无 scatter 负例；日志和两个二进制指纹在 `science/build/clean-leaf-v3-*`。
不以简单修改输出字符串代替编译和真实执行。

`summarize.py` 从归档记录复核完整覆盖、原预算、终止时间和工作量；这是证据汇总检查，
不是新的独立物理 oracle。另以 10 个被刻意破坏的记录验证缺项、超时、容差更改、
NaN、短时运行及比较失败不会被误判通过。

## 修复版 16 GiB Debug 构建

本次八文件修复已重新完成独立的 clean、串行、内置网络 CUDA Debug archive 与 ARCH 链接。
实际源码为 `81c046f6` 加八文件 v3 overlay，不使用先前 S5 的构建资格代替。
专用 cgroup 的 `memory.max=17179869184`、`memory.swap.max=0`，前后 memory.events 全零，
结束时 swap.current 为零。78 次编译调用均成功；最大单编译进程 RSS 为
4,002,584 KiB（Hydro/Tabular4D），不是整个系统或 cgroup 的峰值。
Linux 5.15 未提供 memory.peak，因此总体峰值记为不可用。

此结果是服务器上的 16 GiB 受限 scope，不冒充 16 GiB 实体 WSL；也不覆盖 custom150/200。
采用项目既有 Debug ptxas `-O1`，未变更严格浮点或依赖库。
同时有其他数值/编译任务，构建墙钟仅供资源日志，不能据此比较编译加速。
原始命令、逐 TU 指标、CMakeCache、源码补丁和环境见 `debug16/`。
完整原始构建包也已在服务器与本机分别核验：

- `debug16-fix-build-v1.tar.zst`：127,089,865 bytes；SHA-256 `62ea740df1a1eaea55142fbd6c3ca4ec3584dc70375b4ecde4e135aa3c5cb725`。
- compact：102,795 bytes；SHA-256 `9978bdf3d9a69d4ba95b910de70f55a278b4591a2d1f9be3cffdc4df847b45f5`。
- 服务器 `/home/ubuntu/projects/ARCH-multiphysics-fix-20260914/build/`；本地 `C:/tmp/ARCH-perf-20260909/build/`。

## 不变项与证据恢复

没有修改生产 ODE、EOS、网络、库、严格浮点、科学容差或 restart schema。
BD 详细日志只存在诊断树及证据补丁，不在生产源码中。
八个生产／测试修改文件已逐字节核对本地与受测服务器版本，SHA-256 完全一致。
提交采用 Git 的 CRLF→LF 规范化；原始字节指纹和规范化 blob 对照保存在
`tested-source-identity.json`，不冒称两种行尾的 blob 相同。

`diagnostics/records/` 保留定位 JSON、输入、源补丁、编译命令等；完整逐步日志、
HDF5 和诊断二进制均在 302 个原始文件组成的完整包内。双端完整 SHA 已验证：

- `composition-diagnostics-v1.tar.zst`：7,229,128 bytes；SHA-256 `18d97fa6106082f6367f4cc08434589b3e3c2080521f7f36c13fe3b188b3ee25`。
- `composition-diagnostics-compact-v1.tar.zst`：1,045,004 bytes；SHA-256 `378c1d86495d43c72a1f0ab2a69401b67b9a0eaf970c42694d384f9e609377ae`。
- 服务器 `/home/ubuntu/projects/ARCH-microphysics-20260914/build/`；本地 `C:/tmp/ARCH-perf-20260909/build/`。

本次完整科学包包括原 15 类、关键 128 块、18 组扩展矩阵的原始 HDF5／日志，
受测生产二进制、独立叶测试、源码 overlay、构建与身份记录：

- `fixed-science-v1.tar.zst`：1,601,189,714 bytes；SHA-256 `613405b2b5570bdbb081a95feed5f863b66e699e9afab9a6d1881c79b99abbae`。
- compact：941,782 bytes；SHA-256 `9850ec5f16f8a1e8e70731e2a587b4fd9466479c3ea9431dfdb70b049fd3fb8c`。
- 服务器 `/home/ubuntu/projects/ARCH-multiphysics-fix-20260914/build/`；本地同名包位于 `C:/tmp/ARCH-perf-20260909/build/`。
- 完整包及 compact 均已在服务器和本机分别核验完整 SHA；compact 展开到 `science/`。

原始失败不可用后来通过覆盖；本目录不宣布正式性能、超大网络完整应用或 sanitizer 验收完成。
