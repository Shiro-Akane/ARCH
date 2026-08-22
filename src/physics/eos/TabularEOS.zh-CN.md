# Tabular EOS HDF5 接口

英文规范文本：[TabularEOS.md](TabularEOS.md)。完整运行参数与策略表面由
[`docs/Reference.zh-CN.md`](../../../docs/Reference.zh-CN.md)统一索引。本文件是供
制表工具使用、与源码相邻的版本化契约。

## 适用范围

`eos_type = tabular` 是一个自动分派 3D/4D 表的策略，其输入契约为 ARCH
规范化、各轴等间隔的 HDF5 schema。现有 Shen、LS、SFHo/HS、CompOSE 或
EOSDriver 表通过转换器映射变量、单位、能量零点和组分坐标，并共用同一个 C++
tabular 策略。

`HelmEos` 是读取 Timmes 固定布局原生 `helm_table.dat` 的独立策略。

## 上游表格体系

[Shen 官方发布](https://user.numazu-ct.ac.jp/~sumi/eos/)的是覆盖密度、温度与
质子/电子分数的主表；[CompOSE 软件](https://compose.obspm.fr/software/)读取
自身的通用 `(T, nB, Yq)` 产品，并可输出自身布局的 HDF5；
[StellarCollapse/EOSDriver](https://stellarcollapse.org/equationofstate.html)
也以 EOSDriver schema 发布 Shen、LS、HS 系列 `.h5`。ARCH 将这些产品表示为
公共 tabular 策略下的 EOS 表族。格式兼容性由下述规范化数据集判定，数据集由
保留来源信息的转换器生成。

## 维数自动识别

新文件必须写标量整数 `table_rank`，取 3 或 4。分派器优先读取该元数据，并校验该值与组分轴的一致性。

只为兼容旧 ARCH 表，缺少 `table_rank` 时按以下规则推断：

- 只有 `n_X` 表示 3D；
- 同时有 `n_A` 和 `n_Z`、且没有 `n_X`，表示 4D；
- 轴不完整或两套轴混用会按歧义文件拒绝。

识别过程仅使用 schema 内容，不使用文件名或预定义 EOS 名称列表。

## 公共标量数据集

| 数据集 | 类型 | 含义 |
| --- | --- | --- |
| `arch_eos_version` | integer | 建议写入的 schema 版本；当前 writer 使用 1 |
| `table_rank` | integer | 新文件必需，3 或 4 |
| `thermodynamic_model` | UTF-8 string | `free_energy` 或 `direct`；缺省仅按旧 `direct` 处理 |
| `n_rho`、`n_T` | integer | 等间隔热力学节点数 |
| `log_rho_min`、`log_rho_max` | float64 | 以 g cm^-3 为单位的密度之 log10 边界 |
| `log_T_min`、`log_T_max` | float64 | 以 K 为单位的温度之 log10 边界 |

边界必须有限且严格递增；首尾节点均包含在表中。因此
`dlog_rho = (max-min)/(n_rho-1)`，温度轴同理。

3D 表还需 `n_X`、`X_min`、`X_max`。可选 `composition_axis` 为
`Ye`（也是默认值）或 `species:<已注册名称>`。前者由当前
`SpeciesManager` 计算电子丰度，后者使用指定核素的质量分数。

4D 表还需 `n_A`、`A_min`、`A_max`、`n_Z`、`Z_min`、`Z_max`；
ARCH 从当前组分计算 Abar 与 Zbar。

## 首选自由能模式

设置 `thermodynamic_model = free_energy`，并写一个 float64 数据集：

| 维数 | shape 与 C-order 索引 |
| --- | --- |
| 3D | `free_energy[n_rho, n_T, n_X]` |
| 4D | `free_energy[n_rho, n_T, n_A, n_Z]` |

数值是单位为 erg g^-1 的比 Helmholtz 自由能 `a`，必须全部有限；密度和温度轴
都至少需要五个节点。

令 `x = ln(rho)`、`y = ln(T)`。ARCH 以四阶差分建立直到每个坐标二阶的导数
和混合导数字段，再在 `(x,y)` 上使用张量积五次 Hermite 插值；热力学域内部
达到 C2。组分方向仍为线性：3D 一次混合，4D 依次沿 Zbar 与 Abar 混合。

全部热力学量来自同一个插值势：

~~~text
P               = rho * a_x
e               = a - a_y
cv              = (a_y - a_yy) / T
(dP/dT)_rho     = rho * a_xy / T
(dP/drho)_T     = a_x + a_xx
(de/drho)_T     = (a_x - a_xy) / rho
(dP/drho)_e     = (dP/drho)_T - (dP/dT)_rho * (de/drho)_T / cv
(dP/de)_rho     = (dP/dT)_rho / cv
cs^2            = (dP/drho)_e + (dP/de)_rho * P/rho^2
Gamma1          = rho * cs^2 / P
~~~

全部物理量从同一个势导出，以维持所实现的热力学关系。跨越不连续区域或欠分辨
相边界时的单调性不属于当前插值器的保证范围。若查询得到非正 `cv` 或 `cs^2`，
接口会拒绝该状态。

## 旧 direct 模式

设置 `thermodynamic_model = direct`；只有旧文件可省略它。以与上表相同的
rank shape 写入 float64 `pressure`、`energy`、`sound_speed` 和 `cv`；
`dp_drho`、`dp_dT` 可选。

loader 要求所有值有限，压力、声速和 `cv` 为正，并要求固定密度与组分时能量随
温度严格递增。各物理量仍做顶点三线性/四线性插值，缺失导数在局部估算。direct 路径用于兼容无法由单一 Helmholtz 势表达的上游产品。新的自由能 EOS
表采用上述首选模式。

## 网格间隔与验收

表格分辨率按 EOS 进行收敛验收；曲率、相变与源表精度共同决定所需网格。初始
间隔和验收流程采用以下规则：

- `log10(rho)` 或 `log10(T)` 的间隔大于 0.1 dex 时发出警告；
- 若平滑物理域的每个热力学方向之外都至少留两个 table node，使生产查询使用
  五点中心导数模板，建议从 0.025 dex 开始；
- 若最外层 table node 本身也属于物理域，为使平滑 EOS 达到千分之一以内误差，
  已验证的起始间隔是 0.0125 dex；
- `X`/`Ye`、Abar、Zbar 方向采用线性插值，其验收间隔通过相应轴减半
  确定；
- 将各轴间隔减半，比较 `P`、`e`、`cv`、`cs`、`(dP/drho)_e`、
  `(dP/de)_rho`、温度反解以及实际 hydro/burn 轨迹；
- 采样范围包括单元内部、顶点和全部外边界；
- 对大曲率或相边界分段加密；当前插值器没有单调 limiter。

维护中的理想气体回归跨越两个 decade，两条热力学轴各有 161 个节点
（0.0125 dex），并采样四个热力学角点；3D/4D 自由能表的最大相对误差均为
3.61253e-4。0.025 dex 时内部结果为 7.98367e-4，但包含单边差分外边界后升至
2.74774e-3；0.05 dex 的内部自由能结果为 6.58068e-3。保留的旧 3D
direct/顶点路径另有包含端点的 0.05 dex smoke test，结果为 3.30779e-3。
这些回归数值适用于测试所用的平滑解析 EOS；核物质表保留各自的收敛要求。

~~~bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target tabular_eos_regression
ctest --test-dir build -R tabular_eos_ideal_gas --output-on-failure
~~~

外部 EOS 转换器还必须记录来源、原始单位、能量零点、轻子/光子贡献、组分定义、
有效域及网格减半误差报告。
