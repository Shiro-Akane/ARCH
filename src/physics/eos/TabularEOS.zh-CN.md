# Tabular EOS HDF5 接口

英文规范文本：[TabularEOS.md](TabularEOS.md)。完整运行参数与策略表面由
[`docs/Reference.zh-CN.md`](../../../docs/Reference.zh-CN.md)统一索引。本文件是供
制表工具使用、与源码相邻的版本化契约。

## 适用范围

`eos_type = tabular` 是一个自动分派 3D/4D 表的策略，其输入契约为 ARCH
规范化、各轴等间隔的 HDF5 schema。来源专用转换器可以把 Shen、LS、
SFHo/HS、CompOSE 或 EOSDriver 的变量、单位、能量零点和组分坐标映射到该
契约，但 ARCH 目前不附带这类转换器；上游文件名或 HDF5 容器本身不能证明
兼容。只有经过转换并独立验收的表才共用该 C++ 策略。

`HelmEos` 是读取 Timmes 固定布局原生 `helm_table.dat` 的独立策略。

## 上游表格体系

[Shen 官方发布](https://user.numazu-ct.ac.jp/~sumi/eos/)的是覆盖密度、温度与
质子/电子分数的主表；[CompOSE 软件](https://compose.obspm.fr/software/)读取
自身的通用 `(T, nB, Yq)` 产品，并可输出自身布局的 HDF5；
[StellarCollapse/EOSDriver](https://stellarcollapse.org/equationofstate.html)
也以 EOSDriver schema 发布 Shen、LS、HS 系列 `.h5`。这些是候选来源体系，
不是当前 loader 可直接打开的文件。格式兼容性只能由下述规范化数据集及保留
来源信息的表族专用转换报告确定。

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
| `arch_eos_version` | integer | 仅旧文件可省略；存在时必须为 1，未知版本会被拒绝 |
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
相边界时的单调性不属于当前插值器的保证范围。若查询得到非正压力、比内能、
`cv` 或 `cs^2`，或非有限导数，接口会拒绝该状态。schema v1 明确要求记录能量
零点 shift，并保证所有可达状态的比内能为正，因为流体反解把正 `e` 作为可接受
状态域。

## 旧 direct 模式

设置 `thermodynamic_model = direct`；只有旧文件可省略它。以与上表相同的
rank shape 写入 float64 `pressure`、`energy`、`sound_speed` 和 `cv`；
`dp_drho`、`dp_dT` 可选，其语义分别为 `(dP/drho)_e` 与
`(dP/dT)_rho`。

loader 要求所有值有限，压力、能量、声速和 `cv` 为正，并要求固定密度与组分时能量随
温度严格递增。各物理量仍做顶点三线性/四线性插值；缺失 `dp_drho` 时会扰动
密度并在固定能量下重新反解温度，而不是错误地保持温度不变。direct 路径用于
兼容无法由单一 Helmholtz 势表达的上游产品。新的自由能 EOS 表采用上述首选
模式。

查询超出任一密度、温度或组分边界时不会外推表格。schema v1 会切换到已有的
单原子理想气体 fallback（`gamma = 5/3`）。这是模型不连续点，不代表外部核
EOS 覆盖了该状态。生产表必须用 guard node 包住完整可达域，并记录是否曾进入
fallback。

## 网格间隔与验收

表格分辨率按 EOS 进行收敛验收；曲率、相变与源表精度共同决定所需网格。初始
间隔和验收流程采用以下规则：

- `log10(rho)` 或 `log10(T)` 的间隔大于 0.02 dex 时发出保守警告；
- 对平滑 EOS，若会查询表格最外节点，从约 0.015 dex 或更细开始；本次审计的
  0.015625 与 0.0125 dex 算例都达到包含端点误差小于 `1e-3`；
- 在物理查询域之外保留至少两个 guard node 可避开单边五点边界模板，但不能
  代替各轴减半验收；
- `X`/`Ye`、Abar、Zbar 方向采用线性插值，其验收间隔通过相应轴减半
  确定；
- 将各轴间隔减半，比较 `P`、`e`、`cv`、`cs`、`(dP/drho)_e`、
  `(dP/de)_rho`、温度反解以及实际 hydro/burn 轨迹；
- 采样范围包括单元内部、顶点和全部外边界；
- 对大曲率或相边界分段加密；当前插值器没有单调 limiter。

保留的理想气体审计 sweep 在两条热力学轴上都跨越两个 decade：

| 每轴节点数 | 间隔 (dex) | 全域最大误差 | 内部最大误差 |
| ---: | ---: | ---: | ---: |
| 17 | 0.125 | 0.516063 | 0.118984 |
| 33 | 0.0625 | 0.0494655 | 0.0132374 |
| 65 | 0.03125 | 0.00542707 | 0.00153701 |
| 129 | 0.015625 | 0.000696707 | 0.000194342 |
| 161 | 0.0125 | 0.000361253 | 0.0000942008 |

3D/4D 结果数值相同，因为该解析自由能与组分无关。因此本 sweep 验证自动 rank
识别、布局、差分/插值收敛和边界行为，但不构成非线性组分插值资格。保留的旧
3D direct/顶点 smoke 结果为 `3.30779e-3`。上述数值只适用于平滑解析 EOS；
相变或核物质表没有通用合格间隔，必须保留自己的各轴减半报告。

仓库中的回归命令只检查最细的 161 节点规范化 3D/4D 表和旧 direct smoke。
完整多分辨率 sweep 是一次独立验证审计，其标量证据统一保留在
`validation/eos`，不另行提交 test target。

~~~bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target tabular_eos_regression
ctest --test-dir build -R tabular_eos_ideal_gas --output-on-failure
~~~

外部 EOS 转换器还必须记录来源、原始单位、能量零点、轻子/光子贡献、组分定义、
有效区/相区 mask、原生字段变换或导数，以及网格减半误差报告。当前 Shen 来源
表审计记录在 [`validation/eos`](../../../validation/eos/README.zh-CN.md)；两份
已审计资产都没有被接受为可直接载入的 ARCH 表。
