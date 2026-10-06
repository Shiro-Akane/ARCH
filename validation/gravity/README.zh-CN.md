# 引力验证

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

本模块覆盖给定外力和由密度求解的自引力。
[自引力计算域参考](../../docs/Reference.zh-CN.md#自引力计算域)
说明适用几何与边界。[独立 CPU 场记录](results/p2-20260922/README.md)
检查均匀网格周期／Dirichlet 收敛与失败路径；
[离散记录](../../docs/development/P2PoissonMultigrid.zh-CN.md)
保留原始预算。生产 AMR 与设备证据见下文。

## 常外部重力

下方外部源项测试施加预先给定的重力加速度，不求解流体自身产生的重力。外力应按预期
改变动量与能量，而不改变总质量。耦合案例进一步检查网格细化和组分扩散时
能否保持这项收支平衡。

各份详细记录注明实际受测的源码、程序和输入；模块结果统一汇总在
[验证总览](../README.zh-CN.md)中。

CPU 与 CUDA 共用逐阶段外部重力源项。验证从周期状态开始，其中 $\rho=1$、
$p=1$、$u=0$，常加速度为 $g_x=1$。在 $t=0.1$ 时，精确解为
$u=g_xt=0.1$，密度和压力不变，且 $E=p/(\gamma-1)+\rho u^2/2$。
重力提供动量与能量，其变化通过解析解核对；质量与被动组分保持守恒。

## AMR 与组分扩散耦合

Release 应用记录
通过了三个案例、24 次 CPU/CUDA 执行和十二次后端对比。
端点检查在全部六个物理
时刻端点重新核对了解析源项平衡及质量／组分守恒。两份报告使用相同的源码、程序、
比较工具和依赖库，并检查它们在执行期间保持不变。

[耦合清单](coupled_cases.json)使用一维笛卡尔网格，根网格包含 64 个单元，允许一级
细化。高斯被动组分分布驱动细化；两种气体的比热和绝热指数相同，使密度与压力保持
均匀。RK2、RK3 分别运行重力与 AMR；第三个案例在 RK3 上增加 RKL2 组分扩散，
每个扩散半步使用五个阶段。这些案例关闭热扩散与黏性扩散。

矩阵检查第 1、2、5 步后的混合层级网格，并在 `t=0.1` 比较物理解。
仅含重力的两个案例在终点保留混合层级；扩散案例在终点已粗化回根网格。

| 耦合案例 | 速度 Linf | 压力 Linf | 能量 Linf |
| --- | ---: | ---: | ---: |
| RK2 + AMR | `1.388e-17` | `4.441e-16` | `1.332e-15` |
| RK3 + AMR | `2.082e-16` | `1.998e-15` | `4.885e-15` |
| RK3 + RKL2 组分扩散 + AMR | `6.384e-16` | `2.442e-14` | `6.084e-14` |

表中取 CPU/CUDA 端点误差的较大值。密度误差为零，所有解析场误差均低于原有的
`1e-12` Linf 预算。全部采样对比中的最大后端场绝对差为 `1.443e-15`，满足
`rtol=2e-10`、`atol=2e-12`。

质量漂移为零，组分积分的最大相对漂移为 `2.153e-15`；原有守恒预算为
`rtol=2e-12`、`atol=2e-11`。报告使用按层级加权的和，在此笛卡尔网格上乘以
根单元宽度即为物理体积积分。该归一化不改变相对漂移，绝对预算作用于报告中记录的和。
组分精度与扩散收敛另见[扩散记录](../diffusion/README.zh-CN.md)。

同一受测构建也已通过下文的均匀网格源项测试。整体验收状态统一见[验证索引](../README.zh-CN.md)。

## 复现耦合检查

使用启用测试的 CUDA 构建，包含 `ARCH` 和 `arch_cuda_single_level_validation`。
从仓库根目录运行，并选择新的输出目录：

```bash
export OMP_NUM_THREADS=4
python3 tools/validate_backend_results.py \
  --manifest validation/gravity/coupled_cases.json \
  --arch build-cuda/bin/ARCH \
  --checkpoint-validator build-cuda/arch_cuda_single_level_validation \
  --build-dir build-cuda --source-root . \
  --output-root validation/gravity/results/coupled-new
python3 validation/gravity/results/coupled-final-20260907/check_terminal.py \
  --build-dir build-cuda \
  --report validation/gravity/results/coupled-new/backend-validation-evidence.json \
  --output-dir validation/gravity/results/coupled-endpoints-new
```

端点检查器读取已保存的初始与指定时刻检查点，重新计算源项和守恒检查，
并核对应用报告与输入的身份。两条命令应使用相同的构建和数据。

## 均匀源项测试

`simulation/ExternalGravity/` 中的 `ExternalGravity` 算例和 [`inputs/`](inputs)
中的不可变参数，在均匀网格上单独验证重力源项。
应用记录
复现了下表及 metrics.csv 中的数值，两个后端均到达指定物理终点。

RK2、RK3 对密度、速度、压力和能量采用相同的 `1e-12` Linf 预算。
密度误差为零；由于状态空间均匀，下列每个 Linf 也等于其 L1 和 L2。

| 积分器（CPU 与 CUDA） | 速度 Linf | 压力 Linf | 能量 Linf | 结果 |
| --- | ---: | ---: | ---: | --- |
| RK2 | 0 | 2.220e-16 | 4.441e-16 | 通过 |
| RK3 | 8.327e-17 | 4.441e-16 | 8.882e-16 | 通过 |

若只复现这两个均匀案例，将上方应用命令改用 `--manifest validation/backend/cases.json`
和 `--case external_gravity_rk2 --case external_gravity_rk3`，并选择不同的输出目录。
耦合端点检查器仅适用于它自己的清单。

这些测试验证给定的常加速度，不涉及静水平衡或自重力。保留的 Euler 输入展示预期的
一阶源项能量误差；独立时间收敛检验见[流体验证](../hydro/README.zh-CN.md)。

## 详细复核记录

<details>
<summary>展开源码身份、机器可读数据与执行日志</summary>

下面是供复现与独立核查使用的数据文件，不是使用教程。上文已说明测试方法、结果和误差标准。

- [Release 应用记录 (JSON)](results/coupled-final-20260907/runtime-893/backend-validation-evidence.json)
- [端点检查 (JSON)](results/coupled-final-20260907/endpoints-894/evidence.json)
- [应用记录 (JSON)](../backend/results/uniform-native-20260907/release-874/backend-validation-evidence.json)
- [metrics.csv (CSV)](metrics.csv)

</details>

## 生产自引力

`gravity_type=self` 在 CPU/CUDA 上支持 Cartesian 一至三维全周期、三维孤立边界及
复合 AMR 泊松求解。受测一维球/柱对称 isolated、完整方位角二维极坐标和三维柱/球坐标
也在两种后端运行，包含原点、轴线与极点接合；流体、燃烧和热扩散联动已有验证。
CPU 制造解、独立边界/Gauss、AMR 与重启证据见
[曲线坐标 CPU 记录](results/p11-p12-20260923/README.md)；CUDA 一维解析场、同输入四模块
对照、双向跨后端续算与本机性能见 [曲线坐标 CUDA 记录](results/p13-20260924/README.md)。
指定 Dirichlet／Neumann／线性 Robin 及用户势边界支持三类几何和有效扇区；孤立质量模型仍要求完整方位角。域外质量源和 Jeans 专用细化指标未纳入此范围。
可从 [GravityBox](../../simulation/GravityBox/README.md)
的示例输入开始。[笛卡尔自引力验收](../../docs/development/P5P7GravityAcceptance.zh-CN.md)
记录数值、耦合、重启与设备检查；早期 [周期场记录](../../docs/development/P3P4CompositeGravity.zh-CN.md)
保留其 CPU 周期范围；[径向场与 AMR 记录](results/p8-p10-20260923/README.md)保存椭圆与 AMR 指标。

`run_self_gravity.py --arch <ARCH> --output <新目录>` 检查 Jeans 波、能量、时间阶、
动态 AMR、重启、孤立边界、一维径向场、用户边界及选定耦合（需要 numpy/h5py）；`--quick` 是既有 CTest
解析与拒绝子集。`arch_composite_poisson 3` 检查三维均匀/混合层级制造解，
`contract` 检查失败路径、层级不变量与径向椭圆门槛。
`check_cuda_compatibility.py --cpu-arch <CPU> --cuda-arch <CUDA> --output <新目录>`
检查实际设备引力；`--benchmark-only --benchmark-time 0.1` 测量达到同一物理终点的
本机代表性工作负载。预热后交替运行，`--benchmark-repeats` 默认五次；记录总耗时、
范围与后端场一致性。`--baseline-cpu-arch` 另列旧版／当前纯 CPU 防退化，CPU/GPU
加速比仍使用同一 CUDA-enabled 程序。正式计时与编译、sanitizer 和其他 GPU 负载分开。
`--benchmark-case large-user-boundary --benchmark-time 0.0003` 单列 64³ 用户物理／势
回调场景；旧发布版没有该模型，只参与内置边界的 CPU 防退化比较。
`--benchmark-cuda-host-threads` 明确记录 CUDA 路线的 Host 线程资源，默认 1；
普通 C++ 边界回调在 Host 求值，可用此选项测量与 CPU 相同线程资源下的整程成本。
线程数随结果保存，不能把增加 Host 线程取得的收益归为 GPU kernel 提速。
`user_boundaries.py` 复用 `--cuda-sanitizer`／`--sanitizer-tool` 插桩入口，保留其场、
实际面收支与重启判据；原始工具日志留在本机。
[用户边界验收记录](results/user-boundaries-20261003/README.md)集中列出独立数学、
原生几何／AMR、设备安全、覆盖映射和整程成本，保留短回调轨迹的整程负收益。

[二维/三维四模块算例](../../simulation/SNIaCoupled/README.md)检查带 AMR 的流体、
自引力、燃烧与热扩散联动；原 [Cartesian CPU/CUDA 冒烟记录](results/snia2d-20260923/README.md)、
[曲线坐标 CPU 记录](results/p11-p12-20260923/README.md)和
[曲线坐标 CUDA 记录](results/p13-20260924/README.md)均不等于 SN Ia 解析精度验收。
曲线坐标 CPU 记录也报告用户提供的 FLASH 4.8 归档中 Cellular 算例的二维/三维受控对照，
归档包含本地初值扩展。[当前比较评估](flash/O5OptimizationReport.zh-CN.md)记录了
固定温度燃烧、可选面 EOS 工作、计算量差异和未关闭项。代表性四模块路径为
HLLC/MUSCL/MC＋RK2＋RKL2 热扩散＋BD/DenseLU＋MG＋AMR，采用
Helmholtz/aprox13 并关闭 NSE，不能据此宣称全部策略组合已验收。

[执行成本复核](flash/results/cpu-followup-20261003/README.md)记录用户边界基线之后的
同状态 EOS 复用、原始表读取和多重网格 Host 调度。相同物理终点的 CPU/FLASH
整程成本、原预算下的字段对照及 2D/3D 四模块五步检查分别说明范围，并保留未达到
的性能目标。FLASH 对比属于可选的维护者工作，ARCH 的构建、测试和 CI 独立运行。
