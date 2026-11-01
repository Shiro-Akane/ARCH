# ARCH 验证与确认

英文原文：[README.md](README.md)。

本目录说明流体、扩散、重力、燃烧、AMR、EOS、重启和生成网络的数值测试。
各模块指南介绍参考解、误差预算、测量结果和复现输入。可先从
[功能清单](../docs/Features.zh-CN.md)了解能力，从[参考手册](../docs/Reference.zh-CN.md)
查看参数组合，从[平台支持](../docs/releases/PlatformSupport.zh-CN.md)查看 Linux 与
Windows 的受测配置。

## 测试覆盖

收敛检查衡量网格或时间步细化后误差的变化；守恒检查计入物理源项和边界输运。
独立解析解或制造解用于检查数值精度，CPU/CUDA 对照用于检查执行后端的一致性。
每份测量记录注明实际使用的源码、程序、输入和数据。

<a id="发布候选版本已完成的结果"></a>

| 区域 | 已记录的 CPU/CUDA 检查 | 记录 |
| --- | --- | --- |
| 原生二维柱坐标科学算例 | 15 个轴向案例、306 个方法／EOS 组合对、源项、耦合与新进程续算；按记录的表域和误差预算检查 | [科学结果](backend/results/release-readiness/Cyl6ComprehensiveAdmission-20261019.json) |
| 光滑流体 | PCM/MUSCL/PPM 在 64/128/256 单元上的空间收敛；独立 Euler/RK2/RK3 时间精度；1,016 步周期平流 | [hydro](hydro/README.zh-CN.md) |
| Riemann 与强激波 | Sod 和平面 Sedov 解析剖面、三档分辨率收敛与激波位置 | [hydro](hydro/README.zh-CN.md#sod-激波管) |
| 组分扩散 | 三档分辨率的 RKL1/RKL2 应用；RKL2 二阶空间收敛 | [diffusion](diffusion/README.zh-CN.md) |
| 外部重力 | RK2/RK3 解析源项平衡，含 AMR 和 RKL2 组分扩散耦合 | [gravity](gravity/README.zh-CN.md) |
| 内置燃烧 | 六次 aprox13 应用、十二个 Host 网络/ODE 时间精度控制及单独的 CUDA 策略检查 | [burn](burn/README.zh-CN.md) |
| 动态 AMR | 十个笛卡尔与 24 个曲线坐标案例；守恒迁移及热／黏性／组分耦合，以及笛卡尔三维、圆柱／球坐标二维的完整运行期细化／粗化循环 | [AMR](amr/README.zh-CN.md) |
| 三维 AMR 插桩 | memcheck 与 racecheck 各通过七次 CUDA 执行，覆盖完整八子块细化／粗化／再次细化；报告完整且无报错，原有场与守恒检查通过 | [AMR 设备检查](amr/README.zh-CN.md) |
| 曲线耦合 AMR 插桩 | memcheck 与 racecheck 各通过两次混合层级网格上的三维球坐标热／黏性／组分 CUDA 运行；报告完整且无报错，原有五阶段 RKL2、场与物理体积守恒检查通过 | [AMR 设备检查](amr/README.zh-CN.md) |
| EOS | 十二个规范化表应用案例和 24 个物理终点，以及独立 Helmholtz 和制造热力学参考 | [EOS](eos/README.zh-CN.md) |
| HDF5/restart | 从中间和终态检查点恢复的四个后端方向；原始组分、控制器与输出连续性 | [restart](restart/README.zh-CN.md) |
| 燃烧／AMR 重启插桩 | memcheck 与 racecheck 各通过六次实际 CUDA 运行及全部九次严格比较；安全报告完整且无报错，原生场、控制器和输出阶段检查通过 | [重启安全记录](restart/README.zh-CN.md#发布候选版本已完成的检查) |
| 生成网络与稀疏燃烧 | 六个真实生成／弱反应应用路径、三种 ODE 的 KLU/cuDSS 轨迹及独立弱反应参考 | [network](network/README.zh-CN.md) |
| 内置 NSE | 十六个应用案例与 32 个物理终点，包含独立平衡态及源项能量检查 | [burn](burn/README.zh-CN.md) |
| 几何 | 十二组高精度度量参考；热／组分／黏性空间收敛与径向原点稳定性 | [AMR](amr/README.zh-CN.md#独立几何与扩散检查) |
| 持续运行 | 500 步流体、100 步扩散 AMR；各重启链十二个循环；72 次精确原生燃烧状态回放及七次固定时刻燃烧比较 | [AMR](amr/README.zh-CN.md#持续重网格与续算)、[restart](restart/README.zh-CN.md#持续原生状态恢复) |

原生二维柱坐标使用轴对称 `cylindrical-rz-pwc` 坐标图和三个物理动量分量。
对应记录包含轴向算例和坐标壁面检查；较早的柱坐标极平面记录使用不同坐标图，
其结果仍对应记录中的受测源码。

[CPU/CUDA 集成结果](backend/results/release-readiness/REL4CudaDeviceAndIntegration-20261019.json)
覆盖 17 项资源／批次契约、九种合法坐标壁面组合、公共应用终点、重网格和输入拒绝。
其中 24 步 Cellular 轨迹的全部 25 个检查点通过 ENUC 的 `5e-9` 相对加 `5e-12`
绝对误差预算，数值逐位一致。三对旋转 AMR 通过 `1e-12` 预算，两后端也通过新进程
续算。[燃烧指南](burn/README.zh-CN.md)说明 ENUC 对应的能量诊断含义。

## 运行测试

按[构建指南](../docs/guides/Build.zh-CN.md#build-tests-only-when-you-need-them)配置
测试构建，查看该构建注册的测试并运行所选完整套件：

```sh
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
```

各模块指南提供应用级命令和输入。Python 验证工具使用 NumPy 与 h5py；独立 ODE 或
平衡态参考需要的其他依赖在对应测试中列出。CPU/CUDA 对照需要两个程序和相同的
物理输入，每次测量使用新的输出目录。[后端验证指南](backend/README.md)说明案例组
选择、耦合运行和计时插桩；托管检查见 [CI 说明](../.github/workflows/README.md)。

## 误差约定

数值验证将实现与解析解、制造解或独立收敛解比较；针对实验或已发表物理数据的确认
会单独标识。FLASH 比较属于可选的性能研究，ARCH 测试无需安装 FLASH。

对按单元体积加权的场误差，

$$
L_1(q)=\frac{\sum_i V_i\lvert q_i-q_i^{ref}\rvert}{\sum_i V_i},
\qquad
L_2(q)=\sqrt{\frac{\sum_i V_i(q_i-q_i^{ref})^2}{\sum_i V_i}}.
$$

分辨率 $N$ 与 $2N$ 间的观测阶数为 $p=\log_2(E_N/E_{2N})$。
记录会注明其他范数或归一化方式。AMR 比较把数值场和参考场映射到公共网格，并使用
物理单元体积；轨迹对照采用相同的指定物理时刻。检查点精确恢复另行比较原生状态、
拓扑和控制器元数据。

## 添加或复现结果

使用 [CASE_TEMPLATE.zh-CN.md](CASE_TEMPLATE.zh-CN.md) 记录：

1. 方程、模块、维度、几何和受测性质；
2. 源码提交、程序、编译选项、后端、线程数和硬件；
3. 输入文件，以及外部 EOS／参考数据的校验和；
4. 配置、构建和运行命令，参考来源与采样规则；
5. 场误差、残差或不变量，验收容差及通过／失败结果；
6. CSV/JSON 指标、有用的图，以及保护机制触发或适用范围限制。

各模块把摘要、`inputs/`、指标和可选 `figures/` 放在一起。教学算例归入
`simulation/`，运行表归入 `EOS_toolkit/`。修改结果定义时，一起更新对应输入、指标和
摘要；已标识源码的测量通过其提交和数据校验和复现。

Git 保存紧凑指标、参考采样、参数文件和复现脚本。大体积检查点、Plotfile 与完整
运行轨迹单独索引，见[数据可用性](archive/README.zh-CN.md)。

## 如何理解结果

组件测试覆盖其实际执行的组合。[EOS 指南](eos/README.zh-CN.md)区分规范化表应用与
原生表加载、分量补齐和反演的专项检查；[网络指南](network/README.zh-CN.md)区分
代表性生成／弱反应网络与大网络轨迹、资源测量。GPU 性能可参阅
[CUDA 指南](../docs/CudaBackendStatus.zh-CN.md)，并在自己的硬件上比较代表性输入的
完整运行时间。

<a id="最终候选版本复验"></a>

## 详细复核记录

以下记录保留实际受测源码与误差预算：

- [均匀应用矩阵](backend/results/uniform-native-20260907/release-874/backend-validation-evidence.json)：22 个案例、180 次执行和 90 次比较。
- [生成网络矩阵](network/results/runtime-native-20260907/release-875/backend-validation-evidence.json)：六个案例、48 次执行和 24 次比较，包含 audit31 的 32 方程系统上的 CPU KLU 与 GPU cuDSS。
- [设备插桩](backend/results/final-first-law-20260907/README.md)：memcheck 与 racecheck 各覆盖 23 条路径，包含三种 ODE、两种稀疏存储规模与四段推进。科学运行和 memcheck 使用 `1e-10 s`，racecheck 使用单独的 `1e-12 s` 观察区间。
- [三维 AMR 循环](amr/results/dynamic-3d-final-20260907/release-919/evidence.json)与[曲线坐标 AMR 循环](amr/results/dynamic-curved-final-20260907/release-923/evidence.json)。
- [构建测量](backend/results/cold-core-first-law-20260907/release-909/README.md)：冷构建、增量及无改动构建，包含重型任务上限 2、总并发上限 4 的对照。
- [内存容量](backend/results/device-memory-first-law-20260907/README.md)：重网格迁移、16,384 行 cuDSS 矩阵、生成燃烧与 AMR；观测到的动态分配均已释放。
- [GPU 性能测量](backend/results/hpc-cuda-optimization/README.md)与 [ARCH/FLASH 对照](gravity/flash/README.zh-CN.md)。
