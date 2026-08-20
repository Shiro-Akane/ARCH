# aprox13 单区燃烧

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

> CPU 状态：BD 和 ROS4 通过当前跨求解器容差。CUDA：待完成。

`simulation/BurnOneZone/` 使用生产 burn driver，在 \(\rho=10^7\,\mathrm{g\,cm^{-3}}\)、\(T=3\times10^9\,\mathrm{K}\)、初始 `C12=0.5`、`O16=0.5` 条件下推进至 \(t=10^{-10}\,\mathrm{s}\)。严格 BE_NR 输入（`rtol=1e-10`、`atol=1e-14`）提供内部收敛参考；BD 和 ROS4 使用 `rtol=1e-6`、`atol=1e-10`。这是求解器交叉 verification，不是对 aprox13 反应率的独立物理 validation。

## Helmholtz 表身份

本记录唯一使用的表来源是从 [Timmes EOS 网站](https://cococubed.com/code_pages/eos.shtml)下载的 `helmholtz.tar.xz` 中的 `helm_table.dat`。运行时路径为 `EOS_toolkit/helm_table.dat`，运行前必须由 Git LFS 实体化。

| 属性 | 必需值 |
| --- | --- |
| 表形状 | 541 个密度行 × 201 个温度列 |
| 文件大小 | 60,242,514 bytes |
| SHA-256 | `c9a57c26c6fd2b2b378b9d5295ca1214022f6fec6289d038b47bf8c8938881a1` |

loader 要求固定 541×201 表的全部四个数据块，并拒绝截断或非数值输入。Git LFS pointer 不能作为运行输入。

## 复现

环境与构建来源信息和 [hydro 记录](../hydro/README.zh-CN.md)一致。运行前核对表身份：

```bash
git lfs pull --include="EOS_toolkit/helm_table.dat"
sha256sum EOS_toolkit/helm_table.dat
export OMP_NUM_THREADS=2
for p in simulation/BurnOneZone/*.par; do
  ./bin/ARCH BurnOneZone "$p"
done
```

相对 BE_NR 的验收要求：核素 Linf 和总能量相对误差均不超过 \(10^{-8}\)，丰度和残差不超过 \(10^{-12}\)。在 13 个核素上，L1 为平均绝对差，L2 为均方根差，Linf 为最大绝对差。热力学量使用 \(\lvert q-q_{ref}\rvert/\lvert q_{ref}\rvert\)。

| 求解器 | 核素 L1 | 核素 L2 | 核素 Linf | 相对能量误差 | 结果 |
| --- | ---: | ---: | ---: | ---: | --- |
| BD | 5.086e-12 | 1.093e-11 | 3.280e-11 | 1.538e-11 | 通过 |
| ROS4 | 5.088e-12 | 1.094e-11 | 3.281e-11 | 1.517e-11 | 通过 |

![燃烧求解器比较](../assets/burn_solver_comparison.svg)

两个测试解的丰度和均闭合到舍入误差。ROS4 使用匹配的四 stage L-stable 系数集，每个内部步共享一套 Jacobian 矩阵。Stage 方程和系数集遵循 [L-stable ROS4 公式](https://link.springer.com/article/10.1007/s10915-023-02232-3)，并与 [OpenFOAM Rosenbrock34 实现](https://api.openfoam.com/2212/Rosenbrock34_8C_source.html)交叉核对。本记录只验证一个状态和时间区间；生产结论仍需要容差/子步序列及外部网络参考。网络实现测试见 [Timmes 技术说明](../../src/physics/network/TIMMES_NETWORKS_TECHNICAL_NOTE.zh-CN.md)。
