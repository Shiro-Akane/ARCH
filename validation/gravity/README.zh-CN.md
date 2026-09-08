# 常外部重力

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

这些测试施加预先给定的重力加速度，不求解流体自身产生的重力。外力应按预期
改变动量与能量，而不改变总质量。耦合案例进一步检查网格细化和组分扩散时
能否保持这项收支平衡。

本页结果对应[Validation 总索引](../README.zh-CN.md)注明的科学验收快照；源码组织与
构建检查见单独的[维护记录](../backend/results/maintenance-freeze-20260908/)。

CPU 与 CUDA 共用逐阶段外部重力源项。验证从周期状态开始，其中 \(\rho=1\)、
\(p=1\)、\(u=0\)，常加速度为 \(g_x=1\)。在 \(t=0.1\) 时，精确解为
\(u=g_xt=0.1\)，密度和压力不变，且 \(E=p/(\gamma-1)+\rho u^2/2\)。
重力提供动量与能量，其变化通过解析解核对；质量与被动组分保持守恒。

## AMR 与组分扩散耦合

[Release 应用记录](results/coupled-final-20260907/runtime-893/backend-validation-evidence.json)
通过了三个案例、24 次 CPU/CUDA 执行和十二次后端对比。
[端点检查](results/coupled-final-20260907/endpoints-894/evidence.json)在全部六个物理
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
[应用记录](../backend/results/uniform-native-20260907/release-874/backend-validation-evidence.json)
复现了下表及 [metrics.csv](metrics.csv) 中的数值，两个后端均到达指定物理终点。

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
