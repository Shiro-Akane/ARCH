# 表格 EOS 插值与 Shen 来源表评估

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

> CPU 状态：规范化 3D/4D 平滑自由能表通过间隔 sweep；两份真实 Shen 资产已下载并审计，但未被接受为可直接载入的 ARCH 表。CUDA 待验证。

## 规范化 HDF5 sweep

一次由 tabular EOS 回归派生的独立审计，把解析理想气体 Helmholtz 自由能按五种分辨率分别写入规范化 3D 和 4D HDF5 布局，自动选择 rank，并在各 policy 的固定测试组分下采样 100 个热力学内部点和四个 `rho`/`T` 角点。这里统一保留标量结果；多分辨率审计源码不作为仓库 test target 提交。

C++ sweep 使用以 `affde827fcbf317382ed45372912b562652a71c5` 为基线、并包含本页
记录改动的工作树，以及 GCC 13.3.0、CPU backend、Release flags
`-O3 -march=native -ffast-math -DNDEBUG`，外部来源表分析也在同一台 x86_64
WSL2 Intel Core i7-10700 上完成。该回归本身为串行；周边 ARCH 构建设置了
`OMP_NUM_THREADS=2`。

| 每条热力学轴节点数 | 间隔 (dex) | 全域最大相对误差 | 内部最大误差 |
| ---: | ---: | ---: | ---: |
| 17 | 0.125 | 0.516063 | 0.118984 |
| 33 | 0.0625 | 0.0494655 | 0.0132374 |
| 65 | 0.03125 | 0.00542707 | 0.00153701 |
| 129 | 0.015625 | 0.000696707 | 0.000194342 |
| 161 | 0.0125 | 0.000361253 | 0.0000942008 |

129 与 161 节点表满足全域 `1e-3` 判据。因此，当会查询最外节点时，新的平滑表应从约 0.015 dex 或更细开始。Guard node 能降低边界模板误差，但所有生产表仍必须逐轴减半，尤其要检查大曲率和相边界。该解析自由能与组分无关，所以测试验证 3D/4D 自动识别与布局，但不证明非线性组分轴精度。

另一份非理想 direct-table probe 省略两个导数 dataset，并采用 `e = cv T + alpha rho`、`P = R rho T + K rho^2`，使定能与定温密度导数明确不同。在 `rho = 10`、`T = 1e7` 时，rank-3 与 rank-4 policy 都返回 `2.5001704121322535e15`，与独立定能有限差分逐位一致。解析值为 `2.5e15`（插值相对误差 `6.82e-5`），而定温结果与其相差 `16.67%`。同一份一次性 probe 还确认未知或缺失的现代 schema 版本会被拒绝，构造失败后 dispatch cache 保持干净。

## 官方 Shen EOS4

官方 EOS4 档案来自采用 CC BY 4.0 的 [Zenodo 3612487](https://zenodo.org/records/3612487)。

| 资产 | 字节数 | SHA256 |
| --- | ---: | --- |
| `eos4.tab.zip` | 29,344,007 | `1c47911219a72862a27d4eb3eec765cd38857564f91249d886cb1b8295a8d720` |
| `eos4.tab` | 143,167,115 | `5ee37819f873387af9c38207bcada72a48abe695b9491df9ad5c6a5e69487c34` |

该表共有 650,650 个状态：密度 110 点、间隔 0.1 dex；温度 91 点、间隔 0.04 dex；质子分数 65 点、间隔 0.01。其重子自由能以 MeV/baryon 表示并相对 938 MeV；若要与表内能量零点一致，转换为 ARCH 比自由能前必须增加 6.506 MeV/baryon。把质子分数视为 `Ye` 还需要电中性假设。源文件本身是重子表；构造 ARCH 所需的总 EOS 还必须明确加入电子/正电子和光子分量模型。

直接对该重子势使用 ARCH 五点导数重构，压力相对误差中位数/90%/99% 分位为 `3.89e-5 / 1.20e-3 / 0.1215`，内能误差为 `3.53e-5 / 0.05097 / 0.4295`。源表有 78,335 个负压力状态；重构后 `P`、`cv` 或 `cs2` 非正或非有限的并集为 98,867 个状态。这里不做静默裁零：在提供明确的分量模型与有效区/相区以前，仅重子表不满足当前总 EOS 正值契约。

## StellarCollapse HShen HDF5

EOSDriver 格式 HShen 表来自 [StellarCollapse EOS 集合](https://stellarcollapse.org/equationofstate.html)。其 CC BY-NC-SA 条款不适合在 MIT 项目中普通捆绑，因此只作为外部验证资产。

| 资产 | 字节数 | SHA256 |
| --- | ---: | --- |
| 压缩 H5 | 282,075,158 | `4ae597f50149afa6dc53eb2cf58dd8118f5a40cbbbe011daff405b08219ec3a0` |
| 解压 H5 | 391,264,384 | `3b7c598bf56ec12d734e13a97daf1eeb1f58f59849c5f65c4f9f72dd292b177c` |

其 shape 为 `(Ye,T,rho) = (65,180,220)`，包含 `logpress`、带 shift 的 `logenergy`、entropy、`dedt`、`cs2` 和压力导数，但没有 Helmholtz 自由能。审计先撤销 `2.49119e18 erg/g` 能量 shift，并把 MeV 温度与 `k_B`/baryon entropy 一致地换算为 erg/g，再用 `a = e - T s` 构造自由能并执行 ARCH 差分。相对原生字段的误差为：

| 字段 | q50 | q90 | q99 | 重构非正数量 |
| --- | ---: | ---: | ---: | ---: |
| pressure | `6.90e-5` | `1.388e-2` | `0.21696` | 3,863 |
| cv | `1.043e-2` | `0.6377` | `9.136` | 41,874 |
| cs2 | `1.337e-3` | `0.1509` | `7.177` | 32,514 |
| energy | `1.312e-4` | `1.622e-2` | `0.6151` | — |

把该文件映射到旧 direct 布局同样会改变语义：EOSDriver 插值 `logP` 和 shifted `logE`，而 ARCH direct 当前线性插值物理字段。源表本身还有 7,998 个非正 `dedt` 和 1,443 个非正 `cs2` 状态。因此本结果标为“已评估/未接受”，而不是下载失败或已支持 EOS。

## 转换器边界

这些表族的生产转换器至少需要显式坐标数组与单位、能量零点和 shift 元数据、baryon/lepton/photon 范围、`Ye`/`Yp` 语义、可用时的原生自由能导数、有效区/相区 mask、逐字段 `linear`/`log10`/`shifted_log10` 变换，以及来源/版本/许可信息。当前 schema v1 对平滑、等间隔的 Helmholtz 表仍有效；本次评估明确了真实核物质表接入前所需的扩展工作。

## 维护中的 smoke

~~~bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target tabular_eos_regression
ctest --test-dir build -R tabular_eos_ideal_gas --output-on-failure
~~~

该命令检查 161 节点的规范化 3D/4D 表以及旧 direct 路径，不会重跑上面的完整
五分辨率审计。

大型外部资产与探索性转换数据不纳入仓库；checksum 列于上文，选定的标量结果
记录在 [metrics.csv](metrics.csv)。
