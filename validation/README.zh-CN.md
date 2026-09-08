# ARCH Verification 与 Validation

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

本目录是定量验证记录的统一入口，覆盖 CPU 与 CUDA 上的流体、扩散、外部重力、燃烧、AMR、EOS、重启和生成网络。各项记录注明实际测试的源码、程序与数据版本。

CPU/CUDA 发布范围已通过数值、应用、回归、设备安全、持续运行和资源检查。
下表保留实际受测的源码、程序及科学数据身份；
[候选版本交付审阅](backend/results/final-acceptance-20260907/release-73a9cf50/)
统一保存最终证据清单和源码资产核对记录。

后续的[冻结前维护审查](backend/results/maintenance-freeze-20260908/README.zh-CN.md)
记录 runtime 目录归类与文档整理，并单独链接源码等价性审阅及重编译检查。
下表中的科学结果保留原有受测身份。

Timmes 作者联络及重新分发条款确认仍待补充，具体见
[第三方说明](../THIRD_PARTY_NOTICES.zh-CN.md)。这项发布行政事项与已完成的技术测试分别记录。

## 目录契约

每个模块只拥有一个子目录，其中包含整理后的 README、机器可读指标、`inputs/` 下的不可变基线参数和可选的 `figures/`。原始日志、机型信息和历史记录归入模块的 `results/`，与用户摘要分开。验证 `.par` 应与指标及验收结论同步修改。教学示例保留在 `simulation/`，运行数据保留在 `EOS_toolkit/`，不建立平行验证树。

新生成的 HDF5 检查点和绘图输出保留在本地结果目录，不自动加入 Git。JSON/CSV
指标、参数、日志和文件哈希仍由 Git 保存；需要提供完整二进制数据时，另行发布
数据归档。已有受版本控制的参考数据和 EOS 输入不受影响。

## 发布候选版本已完成的结果

| 区域 | 已通过的 CPU/CUDA 检查 | 记录 |
| --- | --- | --- |
| 光滑流体 | PCM/MUSCL/PPM 在 64/128/256 单元上的空间收敛；独立 Euler/RK2/RK3 时间精度；1,016 步周期平流 | [hydro](hydro/README.zh-CN.md) |
| Riemann 与强激波 | Sod 和平面 Sedov 解析剖面、三档分辨率收敛与激波位置 | [hydro](hydro/README.zh-CN.md#sod-激波管) |
| 组分扩散 | 三档分辨率的 RKL1/RKL2 应用；RKL2 二阶空间收敛 | [diffusion](diffusion/README.zh-CN.md) |
| 外部重力 | RK2/RK3 解析源项平衡，含 AMR 和 RKL2 组分扩散耦合 | [gravity](gravity/README.zh-CN.md) |
| 内置燃烧 | 六次 aprox13 应用、十二个 Host 网络/ODE 时间精度控制及单独的 CUDA 策略检查 | [burn](burn/README.zh-CN.md) |
| 动态 AMR | 十个笛卡尔与 24 个曲线坐标案例；守恒迁移及热／黏性／组分耦合，以及笛卡尔三维、圆柱／球坐标二维的完整运行期细化／粗化循环 | [AMR](amr/README.zh-CN.md)、[三维拓扑循环](amr/results/dynamic-3d-final-20260907/release-919/evidence.json)、[曲线二维循环](amr/results/dynamic-curved-final-20260907/release-923/evidence.json) |
| 三维 AMR 插桩 | memcheck 与 racecheck 各通过七次 CUDA 执行，覆盖完整八子块细化／粗化／再次细化；报告完整且无报错，原有场与守恒检查通过 | [memcheck](amr/results/dynamic-3d-final-20260907/memcheck-914/evidence.json)、[racecheck](amr/results/dynamic-3d-final-20260907/racecheck-915/evidence.json) |
| 曲线耦合 AMR 插桩 | memcheck 与 racecheck 各通过两次混合层级网格上的三维球坐标热／黏性／组分 CUDA 运行；报告完整且无报错，原有五阶段 RKL2、场与物理体积守恒检查通过 | [memcheck](amr/results/curved-native-20260907/memcheck-904/backend-validation-evidence.json)、[racecheck](amr/results/curved-native-20260907/racecheck-906/backend-validation-evidence.json) |
| EOS | 十二个规范化表应用案例和 24 个物理终点，以及独立 Helmholtz 和制造热力学参考 | [EOS](eos/README.zh-CN.md) |
| HDF5/restart | 从中间和终态检查点恢复的四个后端方向；原始组分、控制器与输出连续性 | [restart](restart/README.zh-CN.md) |
| 燃烧／AMR 重启插桩 | memcheck 与 racecheck 各通过六次实际 CUDA 运行及全部九次严格比较；安全报告完整且无报错，原生场、控制器和输出阶段检查通过 | [重启安全记录](restart/README.zh-CN.md#发布候选版本已完成的检查) |
| 生成网络与稀疏燃烧 | 六个真实生成／弱反应应用路径、三种 ODE 的 KLU/cuDSS 轨迹及独立弱反应参考 | [network](network/README.zh-CN.md) |
| 内置 NSE | 十六个应用案例与 32 个物理终点，包含独立平衡态及源项能量检查 | [burn](burn/README.zh-CN.md) |
| 几何 | 十二组高精度度量参考；热／组分／黏性空间收敛与径向原点稳定性 | [AMR](amr/README.zh-CN.md#独立几何与扩散检查) |
| 持续运行 | 500 步流体、100 步扩散 AMR；各重启链十二个循环；72 次精确原生燃烧状态回放及七次固定时刻燃烧比较 | [AMR](amr/README.zh-CN.md#持续重网格与续算)、[restart](restart/README.zh-CN.md#持续原生状态恢复) |
| 核心编译 | 保留优化的 ARCH 冷构建、无改动检查、紧凑路径增量构建及相同工作的串行／并行对照；重型任务上限 2、总并发上限 4 | [构建测量](backend/results/cold-core-first-law-20260907/release-909/README.zh-CN.md) |
| 声明规模的内存容量 | 重网格迁移、16,384 行 cuDSS 矩阵、生成网络燃烧及 AMR 应用；观测到的动态分配全部释放 | [容量测量](backend/results/device-memory-first-law-20260907/README.zh-CN.md) |
| 专项后端设备检查 | memcheck 与 racecheck 各通过 23 条完整路径，包含弱反应热力学与真实生成网络 cuDSS 燃烧；内存及竞争报告均无报错 | [设备检查记录](backend/results/final-first-law-20260907/README.zh-CN.md) |

[均匀网格应用矩阵](backend/results/uniform-native-20260907/release-874/backend-validation-evidence.json)
包含 22 个案例、180 次 CPU/CUDA 执行及 90 次比较。
[生成网络矩阵](network/results/runtime-native-20260907/release-875/backend-validation-evidence.json)
另包含六个案例、48 次执行及 24 次比较，并在 audit31 的 32 方程系统中实际调用
CPU KLU 与 GPU cuDSS。各模块摘要链接独立科学检查，并保留原定预算。

[完整 Release 回归](backend/results/final-first-law-20260907/release-regression-895/evidence.json)
通过 98 项测试，[纯 CPU 回归](backend/results/final-first-law-20260907/cpu-regression-897/evidence.json)
通过 31 项。[完整 Debug 回归](backend/results/final-first-law-20260907/debug-regression-910/evidence.json)
也已通过全部 98 项，没有跳过项。专项 memcheck 与 racecheck 各通过全部 23 条路径。
其中稀疏测试均覆盖三种 ODE、两种存储规模与四段推进，保留原有数值预算。
普通科学运行和 memcheck 使用完整 `1e-10 s` 区间；racecheck 单独使用 `1e-12 s`
观察区间，短插桩运行不替代完整科学轨迹。上表同时链接主机／设备容量与保留优化的
核心构建测量。

### 历史证据

早期[均匀网格应用](backend/results/release-uniform-accounted-20260907/backend-validation-evidence.json)
和[定向设备安全检查](backend/results/accounted-sanitizer-20260907/README.md)
保留原有身份归档。早期安全检查通过十八个路径的内存与竞争检查，不代替当前候选
程序的验收。其他历史记录见各模块摘要。

## 误差约定

Verification 将实现与解析解、制造解或独立收敛参考比较。与实验或已发表物理数据的 validation 会单独标记。

对按单元体积加权的场误差，

\[
L_1(q)=\frac{\sum_i V_i\lvert q_i-q_i^{ref}\rvert}{\sum_i V_i},
\qquad
L_2(q)=\sqrt{\frac{\sum_i V_i(q_i-q_i^{ref})^2}{\sum_i V_i}}.
\]

分辨率 (N) 与 (2N) 间的观测阶数为 (p=\log_2(E_N/E_{2N}))。每条记录必须说明不同的范数或归一化方式。机器可读结果保留为 CSV 或 JSON；当公式、采样输出、命令和指标足以复现结论时，可以不提交数据处理脚本。

## 记录要求

每条完成记录必须说明：

1. 测试性质与模块、方程、维度和几何；
2. 不可变 `.par` 输入，以及外部 EOS/参考资源的 checksum；
3. 配置、构建和运行命令；
4. commit、编译器/flags、OpenMP 数、后端及相关硬件；
5. 参考来源和采样规则；
6. L1/L2 或模块专用残差和不变量；
7. 验收容差与明确通过/失败结论；
8. 保留的 CSV/JSON 指标及有用时的静态图；
9. 保护机制触发或已知实现限制。

AMR 比较必须将参考场和数值场放到已说明的公共网格，并使用物理单元体积。
CPU/CUDA 比较使用相同算例源码、参数、参考和指标定义。物理验收比较同一指定时刻的解；
检查点精确恢复另有状态与元数据检查。

## 最终候选版本复验

已完成的检查在同一冻结源码与科学数据集上覆盖下列区域，并注明 Debug/Release 程序、
依赖库与比较工具的身份。
[交付审阅](backend/results/final-acceptance-20260907/release-73a9cf50/)
统一记录这些证据和受审源码资产。早期版本的证据分别保留；测试通过不自动表示
源码已发布或第三方授权确认已完成。

| 区域 | 参考或检查 | 必需测量 |
| --- | --- | --- |
| Hydro/Riemann | 解析一维 Sod 解 | 密度、速度、压力、能量 L1/L2；特征位置；守恒 |
| 强激波 | 平面 Sedov 相似解 | 剖面 L1/L2、激波位置、能量、对称性 |
| 流体时间积分 | 光滑半离散参考 | Euler、SSPRK2、SSPRK3 随时间步误差 |
| 几何与扩散 | 解析解和制造解 | 物理体积度量、源项平衡，以及 RKL1/RKL2 下热／组分／黏性扩散的收敛 |
| 动态 AMR | 细化、粗化与界面穿越 | 公共网格 L1/L2、拓扑、守恒及曲线坐标面的一致性 |
| EOS | Ideal、Helmholtz 和规范化 Tabular3D/Tabular4D 参考 | 定义域、导数、反演、错误处理及流体／燃烧耦合行为 |
| 燃烧与 NSE | 独立轨迹和平衡态 | 内置／生成／弱反应网络的组分与能量、容差细化、平衡残差和转变 |
| HDF5/restart | 动态 AMR 连续运行与重启续算 | 原始场、拓扑、控制器／输出元数据，以及兼容性和错误检查 |
| CPU/CUDA 策略 | 相同物理与接受的后端选择 | 场／状态 L1/L2、不变量、DenseLU/KLU/cuDSS 选择及显式拒绝 |
| 可靠性与容量 | 完整执行路径和持续工作负载 | 内存／竞争检查、重复重网格／重启、主机内存、swap 和设备分配峰值 |
| 构建复现 | 保留优化的核心冷构建与增量构建 | 源码和数据身份、编译耗时、安全并行配置及内存压力 |

## 后续工作负载与表格式

ARCH 表格 EOS 支持[表数据契约](../src/physics/eos/TabularEOS.zh-CN.md)中规定的
规范化 3D/4D HDF5 布局。Shen 等原生核物质表的转换属于独立的后续扩展；
每个表族都需要明确单位、能量零点、热力学组成和有效范围。
[EOS 验证](eos/README.zh-CN.md)中保留了历史源表评估的入口。

完整的大网络轨迹与规模测量安排在适合其工作负载的机器上进行，
与发布验收中的代表性生成网络和弱反应网络区分记录。
已有结果和复现输入见[网络验证](network/README.zh-CN.md)。

新记录使用 [CASE_TEMPLATE.zh-CN.md](CASE_TEMPLATE.zh-CN.md)。
