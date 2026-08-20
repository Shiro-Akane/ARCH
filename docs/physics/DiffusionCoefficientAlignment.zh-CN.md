# 扩散系数数学对齐验证

英文原文：[DiffusionCoefficientAlignment.md](DiffusionCoefficientAlignment.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

## 来源基准

`diffusion_math.hpp` 是对 AMReX-Astro Microphysics
[`conductivity/stellar/actual_conductivity.H`](https://github.com/AMReX-Astro/Microphysics/blob/6fb41b5f7475b42a06eb5b09ff0520c9f08aa7f0/conductivity/stellar/actual_conductivity.H)
的框架无关 ARCH 适配，审计基准提交为
`6fb41b5f7475b42a06eb5b09ff0520c9f08aa7f0`。保留的许可证以及适配边界见
[`THIRD_PARTY_NOTICES.zh-CN.md`](../../THIRD_PARTY_NOTICES.zh-CN.md)。

## 验证范围

本测试验证 `diffusion_math.hpp` 中 `ConductivityMath` 的数值结果。调用管线为：

```text
HelmEos::evaluate
  -> eos_state_t
  -> SpeciesManager 的 Aion^-1 和 Zion
  -> ConductivityMath::compute_stellar_conductivity
```

测试参数覆盖：

- 温度：`1e7--1e9 K`；
- 密度：`1e4--1e9 g cm^-3`；
- 组分网络：`aprox19`；
- 样本数：10,000 个以上随机状态。

## 比较方法

参考管线和 `ConductivityMath` 管线均以 IEEE 754 双精度执行。输出使用 `std::hexfloat` 表示，并逐项比较底层浮点值。

三个代表状态的传导系数为：

```text
State 1: 0x1.204aa662ac9cfp+32
State 2: 0x1.27bbef6acfaf3p+49
State 3: 0x1.08d38f0ecf089p+58
```

## 结果

| 指标 | 结果 |
| --- | ---: |
| 最大绝对误差 | `0.0` |
| 最大相对误差 | `0.0` |

测试范围内，两条管线的传导系数逐位一致。该结论适用于上述参数范围、组分网络和测试构建；扩展物理模型或数值路径时需增加相应验证。
