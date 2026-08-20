# 常外部重力

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

> CPU 状态：RK2/RK3 通过。CUDA：待完成。

`ExternalGravity` 实现仍位于 `simulation/ExternalGravity/`；本记录归属的不可变参数文件位于 [`inputs/`](inputs/)，它们从均匀周期状态开始，其中 \(\rho=1\)、\(p=1\)、\(u=0\)、常数 \(g_x=1\)。在 \(t=0.1\) 时，精确解为 \(u=g_xt=0.1\)，密度和压力不变，且 \(E=p/(\gamma-1)+\rho u^2/2\)。空间通量散度为零，因此该算例隔离流体时间积分器内部的重力源项。

## 复现

环境和构建来源信息与 [hydro 记录](../hydro/README.zh-CN.md)一致。

```bash
export OMP_NUM_THREADS=2
for p in validation/gravity/inputs/*.par; do
  ./bin/ARCH ExternalGravity "$p"
done
```

RK2 和 RK3 的验收要求密度、速度、压力和总能量的 Linf 误差均不超过 \(10^{-12}\)。Euler 保留用于展示预期的一阶源项能量误差，不用于声明二阶精度。状态保持空间均匀，因此下列每个 Linf 也等于其 L1 和 L2。

| 积分器 | 速度 Linf | 压力 Linf | 能量 Linf | 结果 |
| --- | ---: | ---: | ---: | --- |
| Euler | 0 | 1.006e-4 | 2.515e-4 | 诊断 |
| RK2 | 0 | 2.220e-16 | 0 | 通过 |
| RK3 | 8.327e-17 | 4.441e-16 | 4.441e-16 | 通过 |

该结果验证 CPU 上的常外部重力源路径，不是静水平衡或自重力测试。CUDA 仍为空的一致性行。
