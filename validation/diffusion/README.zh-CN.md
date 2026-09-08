# RKL1/RKL2 组分扩散模

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

本页结果对应[Validation 总索引](../README.zh-CN.md)注明的科学验收版本；后续目录维护
及新构建检查单列于[维护记录](../backend/results/maintenance-freeze-20260908/)。

CPU 与 CUDA 均通过相同的解析误差、有界性和守恒检验。

`DiffusionMode` 实现仍位于 `simulation/DiffusionMode/`；本记录归属的不可变参数文件位于 [`inputs/`](inputs/)，它们在静态、周期一维理想气体状态中推进有界 tracer 质量分数：

\[
X(x,t)=0.5+0.25\exp[-D(2\pi)^2t]\cos(2\pi x),\qquad D=0.01.
\]

参考包含精确单元平均 sinc 因子。只启用核素扩散，背景分数为 `1-tracer`。已提交的 RKL1 和 RKL2 输入使用 64、128 和 256 个单元，终止于 \(t=0.1\)。

## 复现

[最终候选的主程序验证记录](../backend/results/uniform-native-20260907/release-874/backend-validation-evidence.json)保存了两个后端在指定物理终止时刻的结果，以及实际输入、源码、程序和构建身份。当前候选的六个扩散用例均已通过。曲线坐标、动态加密及流体耦合扩散由 [AMR 验证](../amr/README.zh-CN.md)另行覆盖。

```bash
export OMP_NUM_THREADS=4
python3 tools/validate_backend_results.py \
  --manifest validation/backend/cases.json \
  --arch build-cuda/bin/ARCH \
  --checkpoint-validator build-cuda/arch_cuda_single_level_validation \
  --build-dir build-cuda --source-root . \
  --output-root validation/backend/results/uniform-new
```

RKL2 验收要求最后一对分辨率的 tracer L1 阶数至少 1.8，平均 tracer 漂移不超过 \(10^{-12}\)。RKL1 要求分数有限且有界，Linf 误差不超过 \(10^{-5}\)，并满足相同漂移限制。结果由最终 HDF5 单元平均值测量。

下表的显示精度适用于两个后端；[metrics.csv](metrics.csv)分别保留完整精度的观测值。
最终候选运行复现了这些数值。在指定物理终点，CPU/CUDA 场量的最大绝对差为
\(9.992\times10^{-16}\)，满足原有的相对误差 \(5\times10^{-10}\)
和绝对误差 \(2\times10^{-12}\) 对照预算。

| 积分器 | 单元数 | L1 | L2 | L1 阶数 | 平均漂移 |
| --- | ---: | ---: | ---: | ---: | ---: |
| RKL1 | 64 | 1.714e-6 | 1.903e-6 | — | 0 |
| RKL1 | 128 | 3.567e-7 | 3.962e-7 | 2.265 | 0 |
| RKL1 | 256 | 2.221e-7 | 2.467e-7 | 0.684 | 0 |
| RKL2 | 64 | 4.851e-6 | 5.386e-6 | — | ≤1.12e-16 |
| RKL2 | 128 | 1.213e-6 | 1.347e-6 | 2.000 | ≤1.12e-16 |
| RKL2 | 256 | 3.032e-7 | 3.368e-7 | 2.000 | ≤1.12e-16 |

![扩散收敛](figures/convergence.svg)

两个后端均通过。RKL2 序列与二阶空间收敛一致。RKL1 的分辨率与阶段数混合序列用于稳定性、有界性、守恒性和解析误差回归，不测量 RKL1 时间阶数。CUDA 也通过了相同输入下与 CPU 的直接场量对照。
