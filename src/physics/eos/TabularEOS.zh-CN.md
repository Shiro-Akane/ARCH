# Tabular EOS 来源与 HDF5 接口

英文规范文本：[TabularEOS.md](TabularEOS.md)。完整运行参数与策略表面由
[`docs/Reference.zh-CN.md`](../../../docs/Reference.zh-CN.md)统一索引。本文件是供
制表工具使用、与源码相邻的文件格式契约。

状态方程（EOS）将材料的密度、温度和组分与压力、内能等量联系起来。表格 EOS
保存预先采样的数值，不必在每次查询时直接计算所有解析项。HDF5 是文件容器；
下文的数据集名称、坐标轴和单位规定 ARCH 需要从中读取什么。这里的表维数是
热力学坐标轴的数量，不是模拟区域的空间维数。

## 适用范围

`eos_type = tabular` 是一个自动分派 3D/4D 表的策略，目前直接读取 ARCH
规范化 HDF5、原生 EOSDriver/StellarCollapse 总 EOS HDF5，以及原始 Shen
EOS2/EOS4 使用的正温度 16 列重子 ASCII 主表。将 `eos_table_path` 指向来源
即可，不要求用户预先转换，也不增加按 EOS 名称划分的运行时策略。EOSDispatcher
按内容识别格式、加载已支持的表示，在需要时补齐已声明缺少的分量，并将一个
不可变数据 owner 绑定到现有 CPU/CUDA view。

不会按数值或文件名推测未知的成分范围。任意 CompOSE 产品、无关 ASCII 布局、
零温 `.t00` 和零电荷 `.yp0` 辅助产品不在这些读取器的支持范围内。文件接口
兼容并不等于该来源表整个热力学域都已通过科学精度验收。

`HelmEos` 是读取 Timmes 固定布局原生 `helm_table.dat` 的独立策略。

## 上游表格体系

[Shen 官方发布](https://user.numazu-ct.ac.jp/~sumi/eos/)的是覆盖密度、温度与
质子/电子分数的主表；[CompOSE 软件](https://compose.obspm.fr/software/)读取
自身的通用 `(T, nB, Yq)` 产品，并可输出自身布局的 HDF5；
[StellarCollapse/EOSDriver](https://stellarcollapse.org/equationofstate.html)
也以已支持的 EOSDriver schema 发布 Shen、LS、HS 系列总 EOS `.h5`。
这些总表已经包含重子、电子/正电子与光子贡献，ARCH 不会再次叠加完整 Helmholtz
EOS。原始 EOS2/EOS4 主表则只含重子，需使用下述组件补齐。ARCH 随附的原始
文件保留其 [CC BY 4.0 来源与许可](../../../THIRD_PARTY_NOTICES.zh-CN.md)；
加工后的 HShen 表不随附。

## 成分声明与自动补齐

规范化 HDF5 可使用标量字符串 `eos_components` 声明：

| 声明 | 主机加载时补入的分量 |
| --- | --- |
| `baryons` | 电子／正电子与光子 |
| `baryons,electrons_positrons` | 仅光子 |
| `baryons,photons` | 仅电子／正电子 |
| `baryons,electrons_positrons,photons` 或 `total` | 无 |

条目必须已识别、不重复，并包含重子。省略该数据集时，已有规范化表仍按旧有的
完整 EOS 解释，不会被视为请求补齐。EOSDriver 原生格式的契约本身就是总 EOS；
已识别的重子 ASCII 格式则提供明确的仅重子声明。格式适配器负责解释数据，
不另建核物质模型。

补齐要求 `thermodynamic_model=free_energy`；分别插值的压力／能量字段不足以
定义缺失的自由能势。加载器仅加入已有 Helm 电子／正电子组件及共用解析光子组件
中缺少的部分，不会在原有重子之上再次叠加 Helm 离子或库仑修正。
`eos_helm_table_path` 可指定辅助电子表，默认路径为
`EOS_toolkit/tables/helmholtz/helm_table.dat`；总表或仅缺光子的表不会读取该文件。
来源文件不变，用户不必在多种 EOS 策略之间切换，也不必为每次运行维护一份
转换后的配套表。

补电子需要物理 `Ye`：3D 使用 `composition_axis=Ye`（默认值），不能用单一
`species:<name>` 质量分数代替；4D 使用 `Ye=Zbar/Abar`，且必须提供核素元数据。
可选标量 `baryon_mass_g` 必须有限且为正，记录 `rho=m_B*n_B`。定义
`q=1/(m_B*N_A)` 后，在 `rho_H=q*rho` 查询电子组件，并将其比自由能和比内能
乘以 `q`，物理压力不再额外缩放；光子使用来源的实际密度。这是常数单位换算，
不是随密度变化的能量平移，也不是针对 Shen 的拟合修正。省略质量声明时，
保留已有组件提供者的质量约定。

可选标量整数 `nuclear_equilibrium` 只能为 `0` 或 `1`。取 `1`，或原生格式本身
属于核平衡表时，要求 `use_burn=false`：独立燃烧或 NSE 会重复计入核结合能，
并假设平衡表已消去的组分自由度。设为零是物理声明，不是使平衡表与任意反应网络
兼容的开关。

有声明／采用严格域的表，包括非平衡表，也会拒绝 Steger-Warming 通量分裂和
自动恒星热传导。可使用一般 EOS 通量并显式选择常热扩散率，或关闭热扩散。
允许使用动力学核能源项不代表与每种弱反应都兼容：tabular view 尚未提供电子
化学势诊断量 `eta`。尤其是 `aprox19`／`aprox21` 中依赖真实 `eta` 的电子俘获
项，不能据此宣称已与任意 tabular EOS 完成物理兼容。已有 Helmholtz 路径保留其
电子诊断量；此次改动不会另外加入诊断表，也不会替缺失的弱过程输入静默选模型。

有成分声明或标为核平衡的自由能表，无论自动补齐还是预先提供总表，都使用严格
有限域。来源／组件无效性传播到全部导数模板；查询不能跨越屏蔽顶点，也不允许
超出来源或电子表范围。尤其是 `rho_H*Ye` 或温度超出电子表支持时，不会外推
Helm 贡献。若守恒内能需要正性基准，加载时选择一个全局常量，并在整个 owner
寿命中保持固定；压力、熵与热容不变。这不能保证任意插值点正性或单调性，查询
仍须检查可接受性。温度反解检查真实 Hermite 多项式的每个单调区间，拒绝无有效
根或多根，不跨越屏蔽单元，也不替换成理想气体。

owner 指纹绑定来源字节、解释契约及实际使用的辅助电子表精确字节。只修改辅助
表也会改变缓存／重启的 EOS 身份；移动内容相同的文件不会改变身份。

## 原始重子 ASCII 主表

受支持的格式由正温度块、16 个数值列、共同密度与 `Ye` 坐标，以及 EOS2/EOS4
发布的单位／基准组成。对数密度和对数温度网格须满足共用的等间隔导数契约；
物理节点保持不变，可能非均匀的 `Ye` 节点也予以保留。owner 至少需要五个密度
和温度节点。格式解析结果进入同一个通用补齐层，并不是针对 EOS 名称调整物理。

来源约定为 `rho=m_B*n_B`，固定 `m_B=1.66054e-24 g`；压力单位为 MeV fm^-3，
E/F 为 MeV／重子，熵为玻尔兹曼常数／重子。换算使用这一固定来源质量，而不使用
独立打印的密度列比值。来源 E 相对 931.494 MeV，F 相对 938 MeV，因此先用文档
规定的常量 `938-931.494=6.506 MeV` 将 F 对齐到 E 的基准，再进行单位换算；
这不是拟合零点。打印的密度／nB 或 `Ye` 不满足来源精度一致性检查时，屏蔽相应
点，不移动热力学坐标。

插值势由来源 F、`F_ln(rho)=P/rho`、`F_ln(T)=-T*S` 约束，各项都加入对应的
电子／光子贡献。来源压力与熵约束保留了低温热信息，避免仅对舍入后的 F 求差分，
或相减独立舍入的 E/F 时丢失这些信息。运行时能量来自同一个势；独立打印的 E 列
保留用于来源一致性诊断，不作为第二个能量函数。

来源 F/E/S 在打印精度上并非完全相容。已检查的原始数据中，约百分之一的点超出
半个打印末位单位的 F/E/S 残差预算，因此不能声称这种表示逐点满足所有打印列的
该预算。实现不会用随状态变化的零点或拟合修正掩盖差异。有效单元／来源对照与
反解条件数仍须纳入 [EOS 验证](../../../validation/eos/README.zh-CN.md)；
抽样通过不代表整个核物质域都已合格。

## 原生 EOSDriver 总表

读取器要求标量或单元素 `pointsrho`、`pointstemp`、`pointsye`、`energy_shift`，
一维轴 `logrho`、`logtemp`、`ye`，以及 C-order shape 为
`[pointsye, pointstemp, pointsrho]` 的 `logpress`、`logenergy`、`dedt`、
`cs2`、`dpdrhoe`、`dpderho`。每条轴至少有两个有限且严格递增的节点。
读取器保留真实非均匀轴，只转置字段存储、不重新采样。密度单位为 g cm^-3，
来源温度单位为 MeV，`Ye` 是电中性条件下每重子的电荷。温度及热容单位转换
使用共享物理常数。必须提供核素元数据，ARCH 才能从演化质量分数计算 `Ye`。

`logpress` 表示 `log10(P)`，`logenergy` 表示
`log10(e_source + energy_shift)`。ARCH 先插值编码字段，再指数解码。
守恒比内能采用 `e_ARCH = e_source + energy_shift`；有限且非负的来源 shift
是整个运行期间固定的能量基准。因此负的物理来源能量不要求用户手工改零点，
loader 也不会在不同状态选择不同 shift。

热导数与声学量由同一套 `P(rho,T,Ye)`、`e_ARCH(rho,T,Ye)` 插值函数解析求导：

~~~text
cv        = (de/dT)_rho
kappa     = (dP/dT)_rho / cv
chi       = (dP/drho)_T - kappa * (de/drho)_T
cs^2      = chi + kappa * P/rho^2
~~~

这保证所实现的热导数链式法则和牛顿 Euler 声速闭合，但不保证单一自由能势
对应的 Maxwell 关系。来源 `dedt`、`cs2` 与压力导数仍作为参考/质量字段；
它们不替代另一套插值函数的导数。本读取器不通过重构 `a=e-Ts` 再求导实现
这些量。组分导数同样包含对数解码的链式法则。

来源热容或声速非正、力学导数无效、相邻温度节点能量不递增时，对应节点被
排除。查询要求所在单元八个顶点全部有效，同时返回的压力、平移内能、热容和
声速平方有限且为正。超出此域时两种后端都失败，不使用规范化 schema 的理想
气体 fallback。温度反解直接求解实际分段对数线性能量或压力插值函数，检查
残差，并拒绝无根或多根；压力允许负温度斜率，恒压区间不能给出唯一反解。
当前反解扫描所有温度区间，因此应按实际工作负载测量性能。
低温强退化物质即使具有唯一反解，也可能严重病态：相对温度灵敏度为
`e_ARCH/(T*cv)`。来源 `logenergy` 的有限精度会使指定温度容差不可达，
即使能量回代残差很小。真实来源回归会将这类温度欠分辨状态单独报告；原生
文件可以载入，不代表整个表域具有统一的温度精度保证。

这类核平衡总表已包含核结合能，并消去了详细的非平衡组分自由度。ARCH 拒绝
将其与独立 kinetic burn/NSE 源项组合；共用 `Ye` 坐标不代表任意反应网络
在热力学上与之兼容。此读取器不提供电子专用的 `eta`、`pele`、`xne`，依赖
这些量的输运闭合需要另行支持。
启动入口也会拒绝要求纯组分理想气体 gamma 的 Steger-Warming 通量，以及自动
恒星热传导。可以使用一般 EOS 通量，并显式指定正常量 `alpha_therm` 作为
常扩散率模型；也可以关闭热扩散。

表指纹同时包含来源字节和原生解释契约，其中包括能量约定，restart 使用同一
身份检查。没有新增成分声明的旧规范化文件仍保留原来的文件指纹。原生读取沿用已有不可变的
host/device owner 与分派缓存，不会在用户来源文件旁写出转换后的表。

## 规范化 schema：维数自动识别

制表工具必须写标量整数 `table_rank`，取 3 或 4。分派器优先读取该元数据，并校验该值与组分轴的一致性。

读取器也接受缺少 `table_rank` 的文件，并按以下规则推断：

- 只有 `n_X` 表示 3D；
- 同时有 `n_A` 和 `n_Z`、且没有 `n_X`，表示 4D；
- 轴不完整或两套轴混用会按歧义文件拒绝。

识别过程仅使用 schema 内容，不使用文件名或预定义 EOS 名称列表。

## 公共标量数据集

| 数据集 | 类型 | 含义 |
| --- | --- | --- |
| `arch_eos_version` | integer | 存在时必须为 1；仅在 `table_rank` 与 `thermodynamic_model` 都缺失时可省略；其他版本会被拒绝 |
| `table_rank` | integer | 制表工具必须写 3 或 4；读取器的推断规则见上文 |
| `thermodynamic_model` | UTF-8 string | `free_energy` 或 `direct`；缺失时读取器使用 `direct` |
| `eos_components` | UTF-8 string，可选 | 明确的成分声明；取值与旧表行为见上文 |
| `nuclear_equilibrium` | integer，可选 | 只能为 0 或 1；1 排除独立燃烧／NSE |
| `baryon_mass_g` | 正且有限的标量，可选 | 电子组件单位换算使用的固定每重子质量 |
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

设置 `thermodynamic_model = free_energy`，并写一个 float64 势数据集：

| 维数 | shape 与 C-order 索引 |
| --- | --- |
| 3D | `free_energy[n_rho, n_T, n_X]` |
| 4D | `free_energy[n_rho, n_T, n_A, n_Z]` |

数值是单位为 erg g^-1 的比 Helmholtz 自由能 `a`，必须全部有限；密度和温度轴
都至少需要五个节点。可选的 `free_energy_dlnrho` 和 `free_energy_dlnT` 与
`free_energy` 具有相同 shape 和单位，提供已声明来源分量相对于自然对数坐标的
导数。它们是势的约束，不是独立的运行时压力或能量字段；提供这些数据集时，
补齐层同时加入相应导数贡献。

令 `x = ln(rho)`、`y = ln(T)`。ARCH 以四阶差分建立直到每个坐标二阶的导数
和混合导数字段；若有一阶导数约束则以之为起点，再在 `(x,y)` 上使用张量积五次 Hermite 插值；热力学域内部
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

这些恒等式描述插值函数，不构成对来源采样精度的独立证明。若采用固定 owner
能量基准，只向 `e` 加同一个常量，不改变其导数。跨越不连续或欠分辨相边界时
不保证单调性；查询拒绝非正的压力、守恒内能、`cv`、`cs^2` 及非有限导数。
旧的完整规范化表仍须采用有说明的正能量基准；显式声明的自由能来源则使用上文
所述加载时常量基准与严格域处理。

<a id="旧-direct-模式"></a>

## 直接物理量模式

设置 `thermodynamic_model = direct`；该数据集缺失时，读取器也采用此模式。以与上表相同的
rank shape 写入 float64 `pressure`、`energy`、`sound_speed` 和 `cv`；
`dp_drho`、`dp_dT` 可选，其语义分别为 `(dP/drho)_e` 与
`(dP/dT)_rho`。

loader 要求所有值有限，压力、能量、声速和 `cv` 为正，并要求固定密度与组分时能量随
温度严格递增。各物理量仍做顶点三线性/四线性插值；缺失 `dp_drho` 时会扰动
密度并在固定能量下重新反解温度，而不是错误地保持温度不变。direct 路径用于
读取无法由单一 Helmholtz 势表达的产品。direct 表不能声明缺失成分并要求分别
往各字段补入贡献。具有自由能势的表采用上述首选
模式。

查询超出任一密度、温度或组分边界时不会外推表格。规范化 direct 路径保留已有的
单原子理想气体 fallback（`gamma = 5/3`），未声明的旧自由能路径同样如此。
显式声明的自由能表则使用上文的严格策略。fallback 是模型不连续点，不代表外部核
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
识别、布局、差分/插值收敛和边界行为，但不构成非线性组分插值资格。保留的
3D direct/顶点 smoke 结果为 `3.30779e-3`。上述数值只适用于平滑解析 EOS；
相变或核物质表没有通用合格间隔，必须保留自己的各轴减半报告。

仓库中的回归命令只检查最细的 161 节点规范化 3D/4D 表和直接物理量 smoke。
完整多分辨率 sweep 是一次独立验证审计，其标量证据统一保留在
`validation/eos`，不另行提交 test target。

~~~bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target tabular_eos_regression
ctest --test-dir build -R tabular_eos_ideal_gas --output-on-failure
~~~

其他外部格式在声明支持之前，必须记录来源、原始单位、能量约定、轻子/光子
范围、组分定义、有效 mask、字段变换和加密/误差测量。
[`validation/eos`](../../../validation/eos/README.zh-CN.md) 的历史 Shen 评估
保留原始来源与重构结果；原生接口验证与真实核物质表的科学精度资格是两回事。
