# 引力验证

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

[P2 CPU Poisson 记录](results/p2-20260922/README.md)覆盖独立均匀网格场求解器的
周期/Dirichlet 解析收敛、弱密度扰动、CGS 尺度与失败处理；不启用 `gravity_type=self`，
该 P2 记录本身不证明后续生产路径或 GPU 路径。
固定的离散决定和预算见 [P2 契约](../../docs/development/P2PoissonMultigrid.zh-CN.md)。

## 常外部重力

下方外部源项测试施加预先给定的重力加速度，不求解流体自身产生的重力。外力应按预期
改变动量与能量，而不改变总质量。耦合案例进一步检查网格细化和组分扩散时
能否保持这项收支平衡。

各份详细记录注明实际受测的源码、程序和输入；模块结果统一汇总在
[验证总览](../README.zh-CN.md)中。

CPU 与 CUDA 共用逐阶段外部重力源项。验证从周期状态开始，其中 \(\rho=1\)、
\(p=1\)、\(u=0\)，常加速度为 \(g_x=1\)。在 \(t=0.1\) 时，精确解为
\(u=g_xt=0.1\)，密度和压力不变，且 \(E=p/(\gamma-1)+\rho u^2/2\)。
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

`simulation/ExternalGravity/` 中的 `ExternalGravity` 算例和 [`inputs/`](inputs/)
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
[P11/P12 记录](results/p11-p12-20260923/README.md)；CUDA 一维解析场、同输入四模块
对照、双向跨后端续算与本机性能见 [P13 记录](results/p13-20260924/README.md)。
部分方位角扇区、域外质量源和 Jeans 专用细化指标仍不支持自引力。
可从 [GravityBox](../../simulation/GravityBox/README.md)
的示例输入开始。[P5–P7 验收](../../docs/development/P5P7GravityAcceptance.zh-CN.md)
记录数值、耦合、重启与设备检查；早期 [P3/P4 记录](../../docs/development/P3P4CompositeGravity.zh-CN.md)
保留其 CPU 周期范围；[P8–P10 一维径向记录](results/p8-p10-20260923/README.md)保存椭圆与 AMR 指标。

`run_self_gravity.py --arch <ARCH> --output <新目录>` 检查 Jeans 波、能量、时间阶、
动态 AMR、重启、孤立边界、一维径向场及选定耦合（需要 numpy/h5py）；`--quick` 是既有 CTest
解析与拒绝子集。`arch_composite_poisson 3` 检查三维均匀/混合层级制造解，
`contract` 检查失败路径、层级不变量与径向椭圆门槛。
`check_cuda_compatibility.py --cpu-arch <CPU> --cuda-arch <CUDA> --output <新目录>`
检查实际设备引力；`--benchmark-only` 测量本机代表性工作负载。

[二维/三维四模块算例](../../simulation/SNIaCoupled/README.md)检查带 AMR 的流体、
自引力、燃烧与热扩散联动；原 [Cartesian CPU/CUDA 冒烟记录](results/snia2d-20260923/README.md)、
[P11/P12 CPU 曲线坐标记录](results/p11-p12-20260923/README.md)和
[P13 曲线 CUDA 记录](results/p13-20260924/README.md)均不等于 SN Ia 解析精度验收。
P11/P12 记录也报告原始 FLASH 4.8 Cellular 算例的二维/三维受控对照。
