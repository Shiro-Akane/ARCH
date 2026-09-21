# 物理配置与旧 EOS 退役清单

| 项目 | 记录 |
|---|---|
| 基准 | `physics/selfgravity`，`99793b49887418d3fa4f151f9ad6b571b528f708` |
| 日期/状态 | 2026-09-22；P1.5 清理及 CPU/CUDA 验收完成；结果与支持边界见实施记录 |
| 用户边界 | 不要求旧输入、旧接口或已退役 EOS 行为兼容；可整套移除被替代的实现与配套设施，以保留路线的当前实现为基线 |
| 范围 | 全部 90 个标准运行参数；4 个共享 custom-map 控制项；2 个裸配置成员；关联旧入口及 Tabular 新旧路径 |
| 不计入固定参数总数 | case 专有参数、动态核素初始分数、生成网络配方、构建选项和环境变量；它们有自己的登记/读取入口，不能仅因不在标准表中就删除 |

本清单补充[低密度修复计划](LowDensityRobustnessPlan.zh-CN.md)及[自引力主计划](SelfGravityImplementationPlan.zh-CN.md)。
最新用户要求取代此前“为了兼容而保留失效键/旧 EOS”的安排。下文的“当前/拟”保留审计时点口径；实施现状以 [P1.5 实施记录](P1_5ImplementationReport.zh-CN.md) 为准。
“无消费者”指当前仓内生产路径未消费；有消费者但不再维护的路线明确记为主动退役，不能称为死代码。

## 1. 数量与结论

| 类别 | 数量 | 处置 |
|---|---:|---|
| 标准键：当前生效，含条件生效 | 84 | 保留真实控制权，按模型适用性及 Basic/Advanced 展示 |
| 标准键：无算法/诊断消费者 | 4 | 删除解析、成员/view 携带、API/GUI 描述和对应示例项 |
| 标准键：旧别名 | 1 | 删除 `timeintegrator`，只保留 `time_integrator` |
| 标准键：明确预留 | 1 | 保留 `gravity_G` 的身份，self 实装前不可宣称有效 |
| 非标准键：未接入的 C++ 成员 | 2 | 删除 `ignition_temp`、`burn_tol` 及误导注释，不开放为输入 |
| 已生效 custom 项：建议补入标准 Advanced | 3 | `dt_init`、`dt_min`、`tstep_change_factor`；只补登记，不增加物理自由度 |
| 已生效 custom 项：非物理范围 | 1 | `log_dir`，保留；本轮不要求新增专用设置面板 |
| 当前物理新增自由度 | 0 | 已有物理参数/物性登记可表达本次修复所需控制 |
| 后续 self 首版拟新增入口 | 4 | 1 个边界模式 + 3 个求解精度/工作上限；随实际实现才发布 |

标准表核算：`90 = 84 + 4 + 1 + 1`。删除 5 键后为 85；登记既有 3 个时间步键后为 **88**。
如果后续采用下文 4 个 self 新键，届时为 **92**；88 已落实到当前 schema；92 仍仅为后续建议。
2 个裸成员不在 90 中，不能重复计数。EOS schema 数据集、运行状态、缓存和报告字段也不计作 `.par` 参数。
以下共组织为 **17 条清理工作项：PR 7 条、ER 6 组、CR 4 组**。
工作项粒度不同、涉及文件可能重叠，不应解读成 17 个参数或 17 个可整删文件；
这是本轮配置及其关联旧实现的已确认清单，不是全仓所有死代码均已找尽的证明。

## 2. 可以直接删除的配置与依赖

不设计弃用宽限期、别名转译或并行新旧模型。同步更新仓内调用方并完整重编译即可消除源/设备布局依赖；
当前静态证据不能代替重编译与受影响路线验证。

| 编号 | 键/成员 | 当前实际情况 | 整套移除范围 |
|---|---|---|---|
| PR-01 | `enforce_mass_conservation` | `RuntimeParams` 解析，`BurnConfig/BurnConfigView` 保存，生产算法不读取；参考文档已承认不生效 | 标准定义、解析、两个结构字段及 view 构造、GUI 说明、schema 示例。守恒继续由算法保证，不换成另一个“启用正确性”开关。 |
| PR-02 | `burn_verbose_level` / `BurnConfig::verbose_level` | 已解析，无实际日志消费者 | 定义/解析/字段/展示；已找到 10 份跟踪 `.par` 在使用，全部清理其无效键及承诺。已有公共诊断照常工作，不为此新增一套日志级别。 |
| PR-03 | `ode_use_numerical_jac` / `use_numerical_jacobian` | 仅配置和 view 复制，三种 ODE 未据此切换 Jacobian | 定义、解析、`OdeConfig/OdeConfigView` 字段、复制及展示；保留真正使用的 Jacobian/AD 实现。 |
| PR-04 | `ode_freeze_jacobian` / `freeze_jacobian` | 同样没有实际分支消费者；现有方法自行规定矩阵/Jacobian 使用时机 | 移除该设置全链路；不删除方法内部实际复用，也不把现行复用行为伪装成此开关的效果。 |
| PR-05 | `timeintegrator` | 是有效旧别名，`time_integrator` 优先；API 有 aliasOf/sourceKey，测试验证过优先级 | 删除旧键、嵌套读取、alias 分支、示例和旧别名契约测试；新输入只用 canonical 键。它不是失效物理功能。 |
| PR-06 | `BurnConfig::ignition_temp` | 只声明，无标准读取或仓内消费者；与现有激活阈值含义重叠 | 删除成员和 `burn_ignition_temp` 的误导 custom-map 注释；继续使用 `nuclearTempMin`。 |
| PR-07 | `BurnConfig::burn_tol` | 只声明，无读取/算法消费者 | 删除成员；真实精度控制仍用已生效的 `ode_rtol/ode_atol`，不新增通用 burn tolerance。 |

共同依赖落点：`src/data/GlobalDefs.h`、`src/core/config/{StandardParameters,RuntimeParams}.h`、
`src/api/configuration/{Configuration.cpp,ParameterPresentation.h}`、相关 API 示例及 `docs/Reference*`。
`tests/api/configuration/test_configuration.py` 当前硬性核对 90 项与旧别名，清理时应更新到新契约，
同时增加“目录不再承诺失效控制”和“保留参数确实影响有效配置”的检查，而不是只改计数让测试通过。

`RuntimeParams` 当前会将全部输入保存在 custom map，API 也允许未检查的 case 参数。
因此只从标准表删键，可能把旧拼写变成静默无效的 custom 项；已退役的 Core 键应明确拒绝，
不能回退到旧实现，也不能报告已生效。可用小型拒绝集合完成输入校验，不维持旧参数值、布局或转译设施。
正常 case 扩展入口仍保留，不能用“所有非标准键都报错”破坏用户算例。

## 3. 必须保留的用户控制与 Advanced

Advanced 表示界面收起，不代表失去配置权，也不是放置未实现开关的地方。
Core 输出实际适用性，GUI 不复制物理规则；当前 Core presentation 只有分组、关联开关等信息，
高级选项由 GUI 负责人维护；本轮不向 Core 或文本格式增加级别标签，也不创建 AdvancedConfig。

| 内容 | 已有键 | 展示与控制理由 |
|---|---|---|
| 状态下限/上限（3） | `sml_rho`、`min_eint`、`max_eint` | Advanced；决定修复/可接受状态，必须能按科研尺度控制并记录 |
| 燃烧状态与激活（5） | `smallt`、`smallx`、`nuclearTempMin`、`nuclearDensMin`、`enucDtFactor` | Advanced；floor、激活区域和源项步长限制含义不同，不合并 |
| NSE 物理适用区域（2） | `nseTempThreshold`、`nseDensThreshold` | Advanced；`use_nse` 模式选择仍在物理设置中 |
| 材料与输运 | `gamma`、`alpha_therm`、`nu_visc`、`D_spec`，以及现有 SpeciesManager 的 A/Z/Cv/gamma | 所选模型的物理设置中可见，或至少 Advanced；来源/EOS 派生量只读，不虚构第二套物性 |
| 引力强度 | `gravity_g_x/y/z`、`gravity_G` | 外场分量按 external 展示；G 在 self 支持后可 Advanced，默认使用已有 CGS 常数 |
| 科研误差与工作上限（7） | `cfl`、`diff_cfl`、`ode_rtol`、`ode_atol`、`ode_max_newton_iter`、`ode_max_substeps`、`diff_max_stages` | Advanced；保留精度、稳定性与有界运行的用户控制，不能全部硬编码 |
| 已有方法选择/适应步控制 | `solver`、`reconstruct`、`limiter`、`time_integrator`、`ode_solver`、`linear_solver`、`ode_dt_safe_fac`、`ode_dt_fac_max`、`ode_dt_fac_min`、`ode_initial_dt_frac` | 有真实消费者，不在本轮删除；方法细节默认收起，不新增更多微调项 |
| 熵修正 | `EntropyFix`、`EntropyFixCoefficient` | SW/Roe 路径有实际使用，其他通量不使用时应标为不适用，不列死参数 |

尤其应修正“模块已开启就全部 applicable”的粗粒度描述：`ode_max_newton_iter` 当前由 BE_NR 消费，
`ode_dt_safe_fac` 在 BE_NR/ROS4 使用而 BD 未读取；不能因为某一种方法不消费就从所有方法删除。
精度/工作上限在此列出属于控制风险检查，不是重新设计或调节 ODE 方法内部参数。

## 4. 额外需要登记或添加的设置

### 4.1 立即可规划的登记：3 个旧有有效键，0 个新物理旋钮

| 键 | 当前消费者/默认值 | 拟补内容 |
|---|---|---|
| `dt_init` | `driver/schedule/DriverControl.h`、`Driver.h`；`1e-16 s`，用于燃烧初始宏步限制及控制器初值 | 进入标准目录与 Advanced，统一默认来源和有效域；按真实初始步/恢复行为说明 |
| `dt_min` | `DriverControl.h::calculate_next_dt`；`1e-20 s` | Advanced 停止下限；区分工程下限与 `t+dt==t`，正有限域与零的语义先冻结 |
| `tstep_change_factor` | 同一控制器，后续宏步增长上限；`1.2` | Advanced 无量纲增长控制，域检查及恢复轨迹验证 |

只登记和共享校验即可，不强制为每个键新增一个 `SimConfig` 成员；已有读取方式若保留，必须去掉多处默认来源。
也不能同时留下可独立修改的 custom 值和 typed 值。不更改这些默认数字来优化某次验证结果。

### 4.2 self 首版最小新增集：4 个入口，随实现发布

以下名称为建议，在对应实现阶段冻结；不是当前已支持参数。

| 建议键 | 层次 | 必要性与边界 |
|---|---|---|
| `gravity_boundary` | 物理设置 | 首版统一边界模式（如 periodic/isolated），明确与流体边界相容性；不增加任意混合面的额外键。仅出现一个有效支持选项时不伪造可选能力。 |
| `gravity_rtol` | Advanced | 控制原问题残差的相对精度；不复用燃烧 `ode_rtol` |
| `gravity_atol` | Advanced | 控制绝对精度/零 RHS；若采用主计划的体积加权 RMS 与 `laplacian(phi)=4πG rho`，单位为 `s^-2` |
| `gravity_max_cycles` | Advanced | 控制最大工作量，达到上限而残差不达标必须失败；不是固定循环后无条件接受 |

三项求解控制传入通用椭圆/MG 的 solve options，不为每个平滑器再造一套全局参数。
不新增 `use_self_gravity`、另一份 G、`gravity_rho_floor`、FFT 模式、每层 damping 或 pivot epsilon。
多极阶数、Jeans 单元数等仅在相应物理边界/细化能力实际纳入支持时另列必要性，不计入首版 4 项。
现有材料登记已经可控制 Cv，不因空组分 IdealGas 的默认 Cv 就立即增加另一个全局 `ideal_cv`。
取消过的 `state_repair_policy` 不重新加入；修复影响由已有下限、明确失败与不可关闭的修复报告表达。

## 5. 旧 EOS 的明确退役边界

### 5.1 保留的目标集合

- IdealGas 的当前 CGS 实现与材料/组分物性。
- Helmholtz 及其当前源表/电子诊断；它仍是燃烧和分量补齐的实际依赖，不因来源历史较久而删除。
- 规范化 3D/4D **严格自由能**表示：声明物理成分，统一域检查、有效模板、固定能量基准及有残差验收的反解。
- 原生 EOSDriver 总表的严格读取与插值闭合，以及原始 EOS2/EOS4 重子表及其分量补齐。

原生 EOSDriver 的 `native_direct` 与旧规范化 `thermodynamic_model=direct` 不是同一分支。
前者保留非均匀轴、来源 log 编码和固定 shift，是当前有来源测试的输入格式，不能由自由能表无损替代。
删除后者是主动缩减受支持的表格表示；当前仍有 direct 的应用证据，不能声称它已不可达。
上述“保留”是代码维护目标，不把历史特定提交的 CPU/GPU 结果冒称为本轮验证。

### 5.2 六组退役工作，按功能计数

| 编号 | 旧内容 | 删除/收敛后的规则 |
|---|---|---|
| ER-01 缺元数据兼容 | `normalized_rank` 猜 rank；缺 `arch_eos_version` 时放行；缺 model 默认 direct；未声明 components/equilibrium 时绕开部分物理限制 | 规范化输入采用一个明确的新契约，要求版本、秩、模型与物理声明。建议升 schema 版本，仅新版本进入维护路线；原生格式遵循自身完整契约。 |
| ER-02 规范化 direct 双路线 | 3D/4D 的独立 pressure/energy/cs/cv 表、可选 dp 导数、相关插值/上传/拷贝分支 | 规范化只支持 strict free_energy；不写一个 direct→free_energy 猜测转换器。保留原生 3D 所需 direct 存储与数学，不按名字批量删除。 |
| ER-03 宽松域与解析 fallback | 未声明自由能表的非 strict 路径、越界换成固定 gamma 理想气体、用任意 A/Z 或截断值伪造热力学状态 | 域外/缺物性/无有效单元明确失败；删除专门支持这些旧行为的分支与测试期待，不新增 fallback 开关。合法物理真空问题仍由独立规划处理。 |
| ER-04 旧表格温度反解 | 两个 Tabular view 中的旧 Newton、卡到 T_min 后成功、cv 失效后猜导数、迭代尽头返回未经残差验收的温度 | 保留 strict 自由能区间反解和 native 的实际插值函数反解；旧分支连同独有辅助代码退役。 |
| ER-05 旧表身份与耦合豁免 | `source.interpretation.empty()` 返回纯文件 hash；未声明物理成分时跳过耦合检查 | 统一绑定来源、解释契约和实际依赖。旧 EOS checkpoint 身份直接拒绝，无兼容 reader；核平衡/burn、通量和电子输运约束仍严格执行。 |
| ER-06 独占配套设施 | CPU/CUDA direct/fallback 夹具、旧 schema 制表、兼容断言、旧支持矩阵和复现入口 | 删除已退役断言/分支，保留并迁到新契约的科学测试；新规范化表生成直接写完整声明。日志可留作历史证据，不让退休的测试矩阵继续作为当前发布门槛。 |

规范化 schema 的数据集是表文件的物理契约，不是再向 `.par` 添加一堆用户选项。
自由能全部进入严格域后，4D 的 `uses_free_energy/strict_domain` 状态可以随 owner/view 简化；
3D 仍须区分严格自由能与原生编码表，不能删除这种真实表示差异。

### 5.3 不能随手删除的共用反解

`eos_utils::solve_total_energy` **仍由 HelmEos 使用**，除两个旧 Tabular 调用外，
`HelmEos.h::get_total_energy_primitive` 也直接调用它。因此它不是可随旧 Tabular 整段删除的死函数。
其固定阈值、回退循环和最终残差问题应保留在 P1.5 修复范围：为保留的 Helm 路线实现有界、可验收的反解，
迁走最后一个调用后才能删除旧函数体。这里只调整 ARCH 的反解接入，不改 Timmes 来源热力学公式。

### 5.4 文件与配套设施范围

已确认至少 **13 个核心代码文件**涉及 EOS 收敛：

- `src/physics/eos/eosdispatch.h/.cpp`、`eos_Utils.h`（3）；
- `sources/TabularSource.h`、`TabularLoaderUtils.h`、`Tabular3DEOS.cpp`、`Tabular4DEOS.cpp`（4）；
- `tabular/Tabular3DEOS.h`、`Tabular4DEOS.h`（2）；
- `src/cuda/microphysics/eos/owners/tabular3_eos_device_owner.h/.cpp`、`tabular4_eos_device_owner.h/.cpp`（4）。

这是混合文件中的分支/字段收敛，**不是 13 个整文件可删**；目前没有确认可整删的核心 EOS 文件。
Helm 共用反解替换可能再涉及 `HelmEos.h` 外围接入。HDF5/HighFive 仍供当前 EOS 和 IO 使用，不可去掉依赖。
3D/4D CUDA Hydro/Burn 的公开路线仍有保留 EOS 消费者，不因退役 direct 就整条移除。

配套修改至少覆盖 `TabularEOSRegression.cpp`、`TabularCompletionRegression.cpp`、
`test_eos_host_device_parity.cu`、`test_tabular_free_energy_owner.cu`，
`cmake/tests/{HostTests,CudaTests}.cmake` 的受影响注册，以及当前使用的 EOS 应用生成/检查入口。
例如 `validation/eos/results/application-native-20260907/replay.py` 仍生成 direct 和未声明自由能表：
它若保留作为历史复现脚本，应绑定历史版本；当前验证索引改用新契约入口，不能覆盖旧记录假装曾经验证新实现。
`TabularEOS*`、`docs/Reference*`、`validation/eos/README*`、`EOS_toolkit/README*` 和 API 源身份说明同步更新。

## 6. 其他已确认的关联清理

| 编号 | 内容与数量 | 处置及排除 |
|---|---|---|
| CR-01 | 2 个无调用辅助函数：`OdeMath::enforce_mass_conservation`、`enforce_temperature_bounds`，均在 `odeFunction.h` | 可删除孤立函数；当前 ODE 内联的组分约束仍有用途，不能一并去掉。函数与同名配置键分别计数。 |
| CR-02 | 1 个旧头、3 个别名：`src/cuda/microphysics/burn/SparseBeNrBatch.cuh` | 仓内未见消费者，可整文件删除并清理 ownership 描述；保留通用 `SparseOdeBatch` 和 BE_NR continuation。 |
| CR-03 | 1 个无调用虚接口、2 个空覆写：`IGravityPolicy::update_field` | 可删除该方法族，由已有阶段准备接口及未来 domain solve 服务承接；保留被实际使用的 patch source 接口、None/External 功能。 |
| CR-04 | 1 组旧单位展示分支：`legacyCodeUnits`、`code_*` 单位 | 当前 Core 已统一 CGS，可从配置/展示实现删除过期 code-unit 分支；保留角度 rad、真实 unknown 状态与科学单位元数据。 |

这 4 组与 EOS 的 6 组不是参数个数，不能相加后称为“新增/删除了若干配置”。
清点时 checkpoint reader 要求版本 4 和科学身份，未发现完整的旧格式生产 reader；P1.5 为状态控制和修复账本统一提升到版本 5，拒绝旧格式。
`CheckpointCompatibility` 检查的是当前状态身份，不是应删除的历史包袱。
Timmes 转译公式/手工参考 Jacobian 的既有来源豁免继续有效；本次不以“无当前调用”删除来源数学。
已不存在的 predictive-AMR 等旧实现不重复计入工作量。

## 7. 实施与验证边界

1. 先冻结本表的保留/退役集合，更新 schema、示例和真实适用性；配置 PR-01–07、CR 清理可独立提交。
2. 完成 3 个既有时间步键的登记，不添加新的算法微参数。GUI 以新 Core 目录为准，失效控件直接删除；
   不要求新旧交叉使用成功，但版本不匹配须明确拒绝，不能显示错误的配置含义。
3. EOS 按 ER-01–06 一并收敛；不保留 `legacy_eos=true` 或隐藏兼容入口。
   Helm 共用反解先替换并验证，再删除旧实现；保留路线的 P/E/cv/声速、组成导数、反解、无效域和能量闭合继续验收。
4. 退役特性对应的测试可以删除/改为拒绝测试，因为支持集合已获用户授权改变；
   保留特性的独立参考与容差不得降低。旧 Newton 卡死例可变为旧表拒绝，并给新反解保留真正的有界失败检查。
5. CPU 科学验证后集中 CUDA：同时检查新 view/owner 布局、上传/释放、错误汇总及保留科学路线。
   删除几个成员不保证更快；加速结论依然由测量提供。

本清单不直接修改用户未跟踪的 Studio 工程，不删除原始 EOS 数据/许可或历史测量。
需要移除的是当前维护/构建对旧实现的依赖，不是把曾经存在的科学证据抹掉。

## 附录：90 个标准键的完整覆盖

同一行的键共享处置；每个键在本表仅出现一次，供后续核对数量和漏项。

| 分组 | 数量 | 键 | 处置/实际归属 |
|---|---:|---|---|
| Grid 域/拓扑 | 17 | `geometry`, `nblockx1`, `nblockx2`, `nblockx3`, `max_blocks`, `x1_min`, `x1_max`, `x2_min`, `x2_max`, `x3_min`, `x3_max`, `x1l_boundary_type`, `x1r_boundary_type`, `x2l_boundary_type`, `x2r_boundary_type`, `x3l_boundary_type`, `x3r_boundary_type` | 保留；Grid/AmrTree/边界及初始化 |
| Grid AMR | 6 | `lrefinemin`, `lrefinemax`, `regrid_interval`, `refine_var`, `refine_threshold`, `derefine_threshold` | 保留；拓扑、指标与调度；JENS 这个尚未实现的枚举值不是另一个参数 |
| Runtime Hydro | 7 | `solver`, `cfl`, `limiter`, `reconstruct`, `time_integrator`, `EntropyFix`, `EntropyFixCoefficient` | 保留；dispatch、Hydro/CFL、SW/Roe；精确标注适用方法 |
| Runtime 下限 | 3 | `sml_rho`, `min_eint`, `max_eint` | 保留/统一语义 |
| Runtime 执行 | 2 | `compute_backend`, `cuda_device` | 保留；后端解析 |
| Runtime 运行/IO | 11 | `tmax`, `max_steps`, `out_dir`, `base_name`, `plt_dt`, `plt_dstep`, `chk_dt`, `chk_dstep`, `restart`, `restart_file`, `plt_variables` | 保留；Controller/IO/恢复 |
| Runtime 旧别名 | 1 | `timeintegrator` | 删除，PR-05 |
| EOS | 4 | `eos_type`, `eos_table_path`, `eos_helm_table_path`, `gamma` | 保留；当前 EOS 选择及独立表来源，退役旧表格式不删除这些键 |
| Network 物理 | 10 | `use_burn`, `network_name`, `nuclearTempMin`, `nuclearDensMin`, `smallt`, `smallx`, `enucDtFactor`, `use_nse`, `nseTempThreshold`, `nseDensThreshold` | 保留；Setup/Burn/NSE/步长 |
| Network 生效算法控制 | 10 | `ode_solver`, `linear_solver`, `ode_rtol`, `ode_atol`, `ode_max_newton_iter`, `ode_max_substeps`, `ode_dt_safe_fac`, `ode_dt_fac_max`, `ode_dt_fac_min`, `ode_initial_dt_frac` | 保留；方法选择/continuation，按具体方法说明 |
| Network 无消费者 | 4 | `enforce_mass_conservation`, `burn_verbose_level`, `ode_use_numerical_jac`, `ode_freeze_jacobian` | 删除，PR-01–04 |
| Diffusion | 10 | `use_diffusion`, `diff_integrator`, `diff_cfl`, `diff_max_stages`, `use_thermal_diff`, `use_viscous_diff`, `use_species_diff`, `nu_visc`, `alpha_therm`, `D_spec` | 保留；通道/系数/RKL 与稳定性；按 EOS 路线标注 |
| Gravity 当前 | 4 | `gravity_type`, `gravity_g_x`, `gravity_g_y`, `gravity_g_z` | 保留；none/external 有消费者，self 枚举暂不支持 |
| Gravity 预留 | 1 | `gravity_G` | 保留身份，self 阶段接通；不误列死配置 |
