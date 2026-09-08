# AMR 开发记录归档

2026-09-07 JST 整理用户摘要时保留的原文。下方状态与命令对应当时的检查点，
不代表当前发布结论。现状见[AMR 摘要](../../README.zh-CN.md)和
[当前发布计划](../../../../docs/development/CudaReleaseStandard.md)。

# AMR 守恒与细化行为

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

当前版本正在进行发布验证。下文分别说明专项检查与完整应用验收；早期设备测试
保留在[历史记录](../../results/h100-sm90-20260903/README.md)中。

本记录将守恒与细化效率分开验收。下方历史 CPU 数据建立了原始基线；当前 AMR 路径已用一套共享、守恒的 limited-linear 重构替换旧有 coarse-to-fine 分片常数填充，并保留了能够暴露旧版伪细化信号的聚焦回归测试。

## 当前应用验证

当前 Release 候选版本通过了笛卡尔 AMR 矩阵的全部 10 个案例，共 54 次
CPU/CUDA 运行，按预设标准检查拓扑、场变量与守恒量。
[应用验证记录](../../results/release-cartesian-shared-helm-20260907/backend-validation-evidence.json)
包含输入和实际构建产物的标识。

[光滑平流](../../results/restart-smooth-shared-helm-20260907/restart-validation-evidence.json)
和[燃烧及 ENUC 驱动细化](../../results/restart-burn-shared-helm-20260907/restart-validation-evidence.json)
均通过四个 CPU/CUDA 重启方向的检查。每组覆盖重网格后的中间检查点和终止
检查点，并核对能量、时间步控制器及输出历史的连续性。

[Gaussian 初值与热扩散活动检查](../../results/gaussian-initialization-20260907/evidence.json)
覆盖圆柱、球坐标各自的一至三维初值。热扩散开关对照在相同时间和拓扑上，
验证了两个后端都产生可分辨的能量变化。完整演化的曲线坐标矩阵是下一项应用检查。

## 共享几何专项检查

共享几何与扩散的[验证结果](../../results/diffusion-capacity-release-20260907/evidence.json)
用独立的 70/90 位精度积分检查薄径向壳层和南北极附近的单元。CPU 与 CUDA
还通过了三种坐标、一至三维的热／组分扩散及黏性动量／能量通量空间收敛测试。
黏性测试包括均匀笛卡尔速度、二次函数场、变密度场和一维径向情况：每个后端
共 30 组场，分别使用三档网格间距。最细网格的最大归一化误差为 `7.27e-5`，
满足预设的 `1e-4` 门槛。每个后端另有 18 组原点线性径向流平衡检查，以及
6 组实际径向离散矩阵的显式步稳定性检查。每个后端另有 9 组矩阵检查密度比
为 1、10、100 时，实际面输运系数和单元容量对应的稳定步长。热／组分扩散共增加 54 个两端
配对的空间采样，最细网格绝对误差低于 `1.15e-7`，满足 `1e-6` 门槛；
CUDA 内存检查未发现错误。随时间演化的耦合 AMR 另行验证；二维球坐标继续
采用项目约定的极坐标 `(r,phi)`。

在启用测试的构建中编译 `arch_curvilinear_metrics` 和
`arch_cuda_curvilinear_geometry_smoke` 后，用 Python + mpmath 运行：

```bash
OMP_NUM_THREADS=2 python3 -B validation/amr/geometry_reference.py \
  --build-dir build-cuda --output-dir validation/amr/results/my-geometry-run
```

输出目录应为新目录。归档会记录命令、完整的空间测试覆盖、误差门槛，以及运行
前后的源码和构建产物信息。

## 固定算例

已提交四份输入：

- `smooth_uniform80.par`：80 单元均匀网格对照；
- `smooth_uniform160.par`：具有 AMR 最细间距的均匀网格参考；
- `smooth_amr80_l1.par`：80 单元根网格和一级细化，光滑熵波穿越移动粗细界面；
- `sedov_amr_species.par`：带一个输运核素的正则化二维 Sedov 爆炸。

所有算例均使用 Cartesian 几何、理想气体 EOS、HLLC 和 RK3。光滑算例使用 MUSCL-MC，每两步 regrid；Sedov 使用 PPM，并覆盖多维 regrid、reflux 和核素通量。

## 历史 CPU 审计环境

被审计工作树以 `affde827fcbf317382ed45372912b562652a71c5` 为基线，并包含本页
记录的改动；测试使用 GCC 13.3.0、CPU backend、Release flags
`-O3 -march=native -ffast-math -DNDEBUG` 和两个 OpenMP 线程；硬件为 x86_64
WSL2 下的 Intel Core i7-10700。

## 定量结果

范数使用物理单元体积。AMR 与均匀网格比较时，先把 160 单元参考限制到 AMR 叶网格覆盖，再在该公共网格上计算误差。

| 算例 | 测量量 | 结果 | 判定 |
| --- | ---: | ---: | --- |
| 光滑 AMR | 最大质量漂移 | `7.33e-15` | 通过 |
| 光滑 AMR | 最大动量漂移 | `7.33e-15` | 通过 |
| 光滑 AMR | 最大能量漂移 | `2.22e-14` | 通过 |
| 光滑 AMR | 压力 Linf 偏差 | `1.41e-14` | 通过 |
| 光滑 AMR | 密度相对解析解 L1 / L2 / Linf | `7.04e-5 / 1.19e-4 / 4.56e-4` | 通过 |
| 均匀 160 | 密度相对解析解 L1 / L2 / Linf | `2.47e-5 / 5.13e-5 / 2.58e-4` | 参考 |
| AMR 对限制后的均匀 160 | 密度 L1 / L2 / Linf | `5.32e-5 / 1.01e-4 / 4.43e-4` | 通过 |
| Sedov AMR | 质量 / 核素质量漂移 | `1.11e-16 / 1.11e-16` | 通过 |
| Sedov AMR | x/y 动量漂移 | `1.00e-18 / 1.00e-18` | 通过 |
| Sedov AMR | 能量漂移、相对漂移 | `3.55e-15`、`3.43e-15` | 通过 |
| Sedov AMR | 能量质心 | `(0.499924, 0.499924)` | 通过 |
| Sedov AMR | 径向各向异性 | `6.14e-6` | 通过 |

Sedov 拓扑在整个运行中保持 12 个 0 级和 16 个 1 级叶 block。注入能量在单元中心采样，因此守恒量是相对数值初始化能量 `1.0361899940878605` 的漂移，而不是强制等于连续输入值 `1.0`。

一次独立 transfer 审计（不作为仓库 test target 提交）覆盖流体守恒变量及两个 `rho X` 字段。Cartesian 1D/2D/3D 的 prolongation 积分误差分别为 `2.60e-18`、`3.33e-16`、`2.61e-14`，cylindrical 2D 与 spherical 3D 分别为 `2.22e-16`、`1.11e-15`；对应 restriction 误差为 `1.71e-16`、`9.00e-19`、`5.55e-17`、`1.11e-16`、`1.11e-16`，曲线坐标检查使用物理体积。细单元组分闭合误差不超过 `3.33e-16`。一个修复前 `sum(X)` 偏差可达 `0.321` 的强梯度算例，现在同时把核素积分、非负性和闭合保持到 `1.11e-16`。

同一 probe 还包含一个独立分量 limiter 会产生 `-0.605` 内能密度的对抗 Euler 状态。共同凸限制器把所有细单元保持在 `min_eint = 1e-10` 以上，最小比内能为 `1.000036e-10`；五个守恒量的物理体积平均在 Cartesian 下变化为零，在非均匀体积 cylindrical 检查中为 `4.44e-16`。

## 细化问题修复状态

旧审计发现，piecewise-constant coarse-to-fine ghost fill 会把界面 Lohner 指标从 `0.00775/0.00954` 抬高到 `0.06727/0.04230`。该实现现已替换为共享的 limited-linear 守恒变量重构：先重构 `rho X` 再恢复组分，使用统一凸物理状态 limiter，并对 fine-to-coarse restriction 采用体积权重。

保留的 transfer 与 AMR exchange 测试现覆盖 Cartesian 1D/2D/3D、Host 曲线坐标体积加权、X/Y/Z 面、粗细界面两侧及 `Current`/`Next`/`Scratch`。CUDA 只消费 Host-lowered plan 和相同标量插值数学，不维护第二套公式。

Checkpoint v3 持久化 ENUC，两个后端均在 regrid 中迁移它。历史真实设备上的 `refine_var = ENUC` restart 矩阵针对报告中的二进制通过了连续运行和四条 split-run backend route，但不构成当前修改的最终产物验收。旧 v1/v2 checkpoint 仍会将 ENUC 初始化为零，因此不能用于建立 ENUC split-run 等价性。

## 最终产物验收

Restart 同时检查 regrid 后的第 2 步中间 checkpoint（源进程运行到第 3 步）
与第 3 步终态 checkpoint，每种阶段均覆盖 CPU/CUDA 四个恢复方向。每个问题
共 12 次 ARCH 运行、9 次比较，并核验实际元数据和恢复日志。检查点按真实步数
与恢复阶段选择；终态恢复额外产生的 CHK/PLT 编号由源文件推导，物理场、控制器
和拓扑仍按相同标准比较。

证据汇总工具默认核验 AMR；`--profile full-runtime` 还需要 `--curved-matrix`、
`--uniform-matrix` 和 `--generated-matrix`。四组矩阵与两份 restart 报告必须来自
同一构建产物。运行矩阵通过后，仍需完成独立物理、内存安全和容量验收，因此
该工具不会单独给出完整发布认证。

完整命令见[英文版 Final-artifact qualification](README.md#final-artifact-qualification)。整个矩阵与两组 restart 必须使用同一份不再变动的 Git 工作树、构建配置及二进制。两个运行验证脚本均新增必填的 `--build-dir`；目录必须包含 `CMakeCache.txt` 和 `compile_commands.json`，且 CMake 的源码目录必须等于 `--source-root`。需要设置 `BUILD_TESTING=ON` 并同时构建 `ARCH` 与 `arch_cuda_single_level_validation`；只构建应用程序不能提供 checkpoint 比较器。

共享设施 `tools/validation_provenance.py` 记录完整源码 commit、脏工作树内容标识、CMake 配置/选项、编译命令哈希，以及应用程序和比较器各自的 SHA-256。`code-and-validation-inputs-v1` 范围包含 `src`、`simulation`、`tests`、`tools`、`cmake`、`validation` 内代码与验证输入，以及根目录的 CMake 配置；排除文档、归档结果、构建/输出目录和无关的未跟踪用户文件。未跟踪甚至被 Git 忽略的源码仍计入，例如 CMake 可能编译的本地 simulation `.cpp`，不能悄悄排除。本记录描述观测到的工作树，不等同于可复现构建证明。运行期间源码/配置改变、二进制被替换或先修改后恢复，均不能发布成功报告。

还会逐算例记录实际生效的 EOS 表内容哈希，包括 `eos_table_path` 覆盖项及外部路径；相对路径从 ARCH 实际工作目录解析，不能仅记录 `.par` 内的路径字符串。运行结束和汇总门禁均重新核对表身份。验收要求 `ARCH_RUNTIME_OUTPUT_DIRECTORY` 位于 `--build-dir` 内，`--arch` 必须是该配置实际指定的 `ARCH`，比较器必须来自该构建目录；不能用 Release 的缓存给不同配置构建路径中的 Debug 二进制贴标签。这是防止构建混配的路径约束，不是可复现构建证明，也不能识别证据采集前人为覆盖同一路径产物的行为。

三组运行后必须执行汇总门禁，例如：

~~~bash
python3 tools/qualify_cuda_amr_evidence.py \
  --source-root . --build-dir "$amr_build" --arch "$amr_build/bin/ARCH" \
  --checkpoint-validator "$amr_build/arch_cuda_single_level_validation" \
  --final-artifacts "$amr_output/final-artifacts.sha256" \
  --matrix "$amr_output/matrix/backend-validation-evidence.json" \
  --restart-smooth "$amr_output/restart-smooth/restart-validation-evidence.json" \
  --restart-enuc "$amr_output/restart-enuc/restart-validation-evidence.json"
~~~

其中 `amr_build` 与 `amr_output` 按英文示例设置。Debug 与 Release 必须分开构建、完整运行并保存证据，不混合两种配置的哈希；多配置生成器还需给三个验证脚本和汇总门禁指定一致的 `--configuration`。汇总检查拒绝源码/构建身份、应用程序/比较器哈希不一致、矩阵/route 不完整，以及缺少运行后身份核对的历史报告。它不能替代真实运行、冻结 main 的 burn 回归或 sanitizer。保留历史 JSON/日志原样，新验收保存到独立目录。

自包含报告还必须保留解析后的 backend plan、checkpoint 哈希、trace/RKL 完成摘要、准确比较容差，以及 manifest 要求的所有科学精度、守恒和拓扑指标。汇总门禁复用运行验证器的共享检查重新验算这些记录，不接受仅剩 `passed: true` 的稀疏报告。

## 曲线坐标应用验证矩阵

[`gpu_curvilinear_cases.json`](../../gpu_curvilinear_cases.json) 定义了 24 个算例，
分别检查单独的组分扩散，以及热、黏性和组分扩散的耦合；两组均覆盖圆柱／球坐标
一至三维和 RKL1/RKL2。完整应用验证正在进行，Cartesian 矩阵作为另一组测试保留。

所有算例均复用同一份 [`gaussian_diffusion_amr.par`](../../inputs/gaussian_diffusion_amr.par)，
CPU/CUDA 使用完全相同的科学参数。脉冲中心为物理笛卡尔位置 `(1.5, 0, 0)`，
细化层级为 0--1。一维环域 `1 <= r <= 2` 使用 4 个根 block、容量 32；二维、
三维域在保持径向网格间距不变的情况下扩展到 `r = 3`，保留外侧静止区以覆盖
粗细交界面。二维使用 8×2 根拓扑／容量 128，三维使用 8×2×2／容量 512。
所有活动边界均为反射边界。

组分用例保持均匀压力和零速度。耦合用例增加 20% 的压力脉冲和非零本地速度分量，
热扩散率为 0.2、运动黏度为 0.1；两组的组分扩散率均为 0.5，并使用相同的
理想气体 EOS 和组分细化阈值。Gaussian 模块从 Grid 提供的物理笛卡尔坐标
计算一份包络，三维球／圆柱坐标的三个方向均参与计算。普通输入参数 `xc`、
`yc`、`zc` 和 `width` 定义中心与宽度；`pressure_amplitude` 是相对压力幅度，
`u_amplitude`、`v_amplitude`、`w_amplitude` 是本地正交基下的速度幅度。
四个扰动幅度默认均为零，CPU 与 CUDA 共用初始化。
这些被动气体用例指定 `network_name = none` 和 `gas_cv = 717.5`，让压力扰动
形成真实温度梯度并驱动热扩散。其他问题仍可显式选择核反应网络。

[初始化与热扩散检查](../../gaussian_reference.py)独立核验六种曲线坐标初始状态，
再在两个后端分别比较相同物理时间、相同拓扑下的热扩散开／关结果，要求能量
变化显著大于舍入误差。编译 ARCH 和 checkpoint 比较器后，安装 NumPy、h5py 并运行：

```bash
OMP_NUM_THREADS=4 python3 -B validation/amr/gaussian_reference.py \
  --build-dir build-cuda --output-dir validation/amr/results/my-gaussian-check
```

RKL1/RKL2 各检查 2、5 个 accepted steps，hydro RK2 保持启用，每步检查 regrid。
stage cap 为 5，沿用原矩阵的 RKL1 四阶段、RKL2 五阶段以及每步两个 Strang half-lane
检查。局部初始脉冲设计用于产生混合层级；现有 manifest 门禁检查观测 checkpoint
中存在细化和混合层级。Refine/derefine 转换和 restart 连续性另有测试；空间收敛
与原点平衡由上文的独立几何测试覆盖。本矩阵检查这些算子在随时间演化的 AMR
应用中是否正确协作。

物理体积加权守恒仅检查质量、能量及各 `rhoX`；径向动量受到几何源项和壁面力影响，
仍做 CPU/CUDA 场比较，但不作为全局不变量。预算采用与现有 1D AMR/RKL 清单相同的数值：
守恒 `rtol = 2e-12`、`atol = 2e-11`，场比较 `rtol = 1e-8`、`atol = 5e-12`。
不得根据运行结果放宽容差，也不能把 Cartesian 解析 Gaussian 参考直接用于径向方程。

新清单显式声明 `conservation_policy.measure = "physical_cell_volume"`，指标要求
`arch_cuda_single_level_validation --metrics CHECKPOINT --parameters ACTUAL_RUN.par`。
版本 3 checkpoint 不包含物理边界与 root block 数量；validator 会传各 lane 真正运行的
渲染参数（含 overrides），记录参数 SHA-256 和测度。几何重建复用现有 `RuntimeParams`、
`Block::InitGeometry` 与 `GridMetrics::CellVolume`；参数或测度缺失/不匹配时明确失败。
运行待验证矩阵前必须重编比较器。无参数 Cartesian CLI 与旧清单仍使用历史
`2^(-dimension*level)` 归一化权重，保留原绝对预算单位，不静默改成物理体积。
新物理体积预算独立声明为上述数值，不声称它与旧归一化预算具有相同单位。
最终产物汇总门禁也仍要求原 Cartesian 矩阵，未来
曲线坐标报告应单独保留为补充证据。输入检查命令见
[英文说明](README.md#pending-curvilinear-gpu-matrix)；解析成功不构成物理验证通过。

## 历史 CPU 算例复现

~~~bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 4
export OMP_NUM_THREADS=2

./bin/ARCH SmoothAdvection validation/amr/inputs/smooth_uniform80.par
./bin/ARCH SmoothAdvection validation/amr/inputs/smooth_uniform160.par
./bin/ARCH SmoothAdvection validation/amr/inputs/smooth_amr80_l1.par
./bin/ARCH Sedov validation/amr/inputs/sedov_amr_species.par
~~~

标量结果保留在 [metrics.csv](../../metrics.csv)，原始 HDF5 输出不纳入版本控制。已有历史图仅保留为定性诊断，不参与本次验收。

| 历史算例 | 可见模块 | 档案 |
| --- | --- | --- |
| Sedov | 流体、激波驱动细化 | [图像](../../figures/legacy/sedov.png) |
| Gaussian | 扩散、移动细化模式 | [图像](../../figures/legacy/gaussian.png) |
| Rayleigh--Taylor | 流体、重力、扩散 | [图像](../../figures/legacy/rayleigh_taylor.png) |
| Cellular burn | 流体、燃烧 | [图像](../../figures/legacy/cellular_burn.png) |

PPM 在粗细面使用 MUSCL-MinMod，因此本记录不声明 AMR 全域三阶空间收敛。
