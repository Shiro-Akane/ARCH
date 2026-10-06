# Studio 配置接口 v3

当前集成分支的 Core 已实现配置扩展版本 3，外层仍为 schemaVersion="1.0"。
Host/Studio 已消费 v3 的 nullable 表单、来源与诊断；静态 inspection 的坐标、扩散和 AMR
选择摘要已从共同解析结果接线。生产 Driver/dispatch 使用私有构造的只读
RuntimeConfiguration，见下文的运行边界。它们是已实现的接口能力，不代表 O7.0
或联合阶段已经发布：旧输入迁移、完整材料来源展示、受影响科学与平台验收仍分别核对。
v2 的 defaultValue/defaultSource、缺项回填和 custom 未检查列表不再是本接口。

完整字段定义见 [v3 协议](CONFIGURATION_V3_CANDIDATE.md)；
必填／默认规则唯一来源仍是
[配置完整性计划](../../docs/development/ConfigurationContractPlan.zh-CN.md)。

## 入口与边界

边界选项由既有 `options/applicability` 字段发布：物理面可选 `user/neumann`，
势边界可选 `dirichlet/neumann/user`。两类回调使用同目录的
`physical_boundary.cpp`、`gravity_boundary.cpp`，说明其 Host 执行和边界传输成本。
客户端应读取目录中的选项；配置 inspection 不执行回调，也不证明该算例已编译或可运行。
O8 边界选项随当前 v3 目录发布；标准参数数量以选定 binary 为准。

调用 build-cpu/bin/ARCH --config-schema 查询目录。
调用 build-cpu/bin/ARCH --inspect-config Sod --config-stdin --request-id editor-001
并通过 stdin 提供原始参数文件。可用输入见
[迁移后的 Sod](examples/configuration-v3-candidate/sod-valid.par)。

stdin 限制 1 MiB、stdout JSON 含换行限制 8 MiB。退出码 0 表示本层声明检查
完成，2 为请求错误，3 为无效／不完整／条件未定，7 为响应超限。
空输入是缺失配置，不是默认配置。精确原文字节参与 SHA-256 identity；
caseId 和 requestId 保留，Host 还须关联项目、binary/build、会话与编辑版本。

inspection 仅调用共同输入解析及登记的静态 case 声明：
**不构造 SimConfig、不执行 Setup/Init、不加载 EOS、不检查路径、
不初始化 CUDA、不创建输出或推进时间。**
模型的动态物理域、Setup 修改和运行资源仍需后续检查；例如 Sod x_pos
的 Setup 物理域拒绝不由本层冒充完成。simulationReadiness 永远为 not_checked。

## Schema

parameters 是当前活动标准目录；本 binary 为 95 项，gravity_G 进入 retiredKeys。模型声明从同一 binary 的 registry 取得，本次集成包含原有 14 个模型与 O8 的 UserBoundary/UserGravity 示例；Host 不应写死数量。
25 个允许登记默认通过 allowedDefault 发布，其他项为 null。
这些数量仅是本次 binary 证据，客户端不得固定数量。

每项保留 key/type/group/presentation/options/path/units/constraints，
增加 usage、requirement、applicability、allowedDefault、templateRecommendations。
条件由 Core 发布 ID 和 dependencies，由 inspection 返回三态结果；
前端不能执行 description，也不能用缺少开关推断关闭。
推荐模板与允许默认分离；目前 templateRecommendations 为空。
constraints.complete=false，不代表全部模型物理规则已在 schema 枚举。

caseDeclarations 包含当前注册模型的来源路径、源码 SHA 和静态参数声明。
集成 binary 还包含 O8 用户边界示例；模型数量以 registry 实际响应为准；网络组分目录依赖选定 network，在 inspection 展开。
动态依赖未知时不宣称所选模型全部可验证。auxiliaryParameters 当前含 log_dir，
缺失时沿现有 main 所有者从 out_dir 派生，不是新的物理默认。

required/conditional 参数不得因旧 Get 的 fallback 参数而获得默认许可。
合法显式 false、0 和空字符串都保留，typed 值不会规范化回原文。

## Inspection

parameters 展开标准、所选 case、组分和辅助输入，并保留 caseId。
每条记录包含：

- inputState：missing / present / invalid / duplicate。
- rawValue、locations：原始未 trim 值跨度与 UTF-8 字节行列；缺失无伪造位置。
- parsedValue：仅显式输入的严格转换；缺失为 null。
- resolvedValue：Setup 之前共同解析层结果；不等于 Preview model-read 值。
- valueSource：input / case-defined / derived / documented-default 或 null。
- sourceEvidence：登记默认、模型定义与派生依赖的 owner/dependencies。
- requirement/applicability：satisfied / not-applicable / unknown-dependency。
- units/path：Core 声明，文件存在性始终未检查。

格式错误保持 raw、parsed/resolved 为 null；范围错误保留已转换 parsed，
清空 resolved/source。重复键保留所有位置，但不选第一或最后一项。
稀疏组分保留原始比例，不执行归一化；省略组分的零由网络声明提供，
source 为 case-defined，parsed 仍是 null。整个组分的正有限和仍校验。

缺项、无效类型、退役、未知方法、未知键和跨字段问题聚合到 diagnostics。
每项含 code/severity/parameterKey/module/conditionId/expected/locations/relatedKeys。
条件未定使用 UNRESOLVED_DEPENDENCY，不能伪装成依赖字段缺项。
模型／组分声明未完成时不进行猜测式 unknown-key 判断，并降低 coverage。
gravity_G 与历史五个退役键均不能落入 custom。

coverage 分别声明标准、辅助、case、条件和诊断覆盖。
completeness.scope 为 declared-configuration-before-setup，state 为
complete/incomplete/invalid/undetermined；不等于 simulation ready。
响应过大时保留已确认 identity，返回完整受限 JSON、RESPONSE_TOO_LARGE
和 false coverage，而非截断的成功文档。

## 坐标、单位和派生展示

schema.coordinateSystems 继续从 Core 网格轴定义发布各几何/维数模板；
fieldUnits 来自 Core 物理单位。标准输入为 CGS，包括 IdealGas，不自动换算。
units.status 区分 known、dimensionless、not-applicable、coordinate-dependent、
mixed-state、not-specified；未知单位为 null。

v3 inspection 不通过带默认值的 SimConfig 构造 coordinates、diffusion 或 amrIndicators。
当前三个摘要分别消费已解析的拓扑、EOS/扩散开关和 AMR 选择依赖；依赖未解析时返回
null，Host 将其作为缺少可用摘要处理。具体覆盖见本文末尾的三个 partial summary 条目。
这些摘要不是网格构造、EOS 求值或初始 AMR；不能从 v2 快照恢复虚假默认摘要，
也不能用现有 Preview 的状态替代当前未完成配置。

## 客户端与验证

不兼容版本明确提示更新并保留 Working Copy，禁止 null 转 0/false 或旧默认。
草稿保存与运行资格独立；保存、inspection 都不能自动插入缺项或删除退役键。
未知／错误输入保持可定位，显式删除可 Undo。

配置 v3 的检查入口包括 configuration_v3_contract；共同解析与实际入口还由
configuration_input、input_resolution、case_configuration、config_input_records、
configuration_entry_contract 覆盖。它们的通过范围必须关联实际 source/binary/输入，
不能称为全部 Core/Host/Studio 或科学回归通过。
实际 v3 配置响应存于 examples/configuration-v3；原 examples/configuration 是历史
v2 证据，不作为当前客户端协议期望，也不因当前检查通过而改写历史文件。

当前旧模型输入的复验见
[全模型输入审计](../../docs/development/FullModelCurrentInputAudit-20261004.zh-CN.md)：
16 profiles 覆盖该 binary 的 14 个注册 case，15 份声明完整；旧 CellularDet 输入缺
tmax，仍为失败。已批准的 t=0 替代输入与旧 burn-on 输入分开记录。
该静态结果不执行 Setup/Init，也不证明 simulation readiness。

## 受控运行边界

配置准备后的合法策略解析（例如 use_nse=auto）由生产启动所有者完成，再构造
[RuntimeConfiguration](../core/config/RuntimeConfiguration.h)。其构造函数私有，
仅 DispatchSolver 可构造；对象以 const 持有有效配置及已准备的 species。
Driver 与 dispatch bindings 接收该类型，不能用原始 SimConfig 或另一份
SpeciesManager 替代。输入、checked preparation 与策略解析后的运行值仍为不同层。

既有 CPU t=0 拓扑对照记录于
[运行边界摘要](../../docs/development/O7RuntimeConfigurationT0Summary.json)。
该证据仅覆盖所记身份的初始拓扑，不表示 CUDA、演化、所有下层直接入口或整个
材料来源呈现已经完成；后续验收继续按联合计划分别记录。

### Registered model values during configuration preparation

A Core case declaration may supply absent standard inputs in
CaseConfiguration.standard_values. Each ModelInputValue names an active key,
an exactly typed value, CaseDefined or Derived source, and named owner evidence.
Derived values additionally list their standard-input dependencies. The registered
case source file and source SHA must be present; the loaded input snapshot retains
them. This is a C++ model contract, not browser input or a new user defaults file.

These values go through the existing scalar, option, relation and requirement
checks before Setup. Explicit tokens remain authoritative, including invalid
tokens: a provision cannot replace invalid input. An absent provided key retains
Missing raw state, null parsed value and no fabricated source line, while its
resolved value and case-defined/derived source are available. Approved optional
defaults retain their separate documented-default source.

Consumer conditions are evaluated again after model values resolve. Provisions
must remain stable across that evaluation; changing declarations, unknown/retired
keys, duplicate provisions, wrong types, missing source evidence, and missing or
cyclic dependencies fail. No Setup, EOS or filesystem resources are used for this
analysis. The current production cases have not gained new implicit values.

This implementation covers standard-input provisions. Arbitrary Setup field
assignment, custom-parameter provisions and material registration provenance are
not made valid by this mechanism; their remaining migration is tracked separately.

### Model reads after loading

For a successfully loaded configuration, SimConfig.Get reads the checked case,
composition or auxiliary records. A missing declaration raises
UNDECLARED_PARAMETER_ACCESS; consuming a declared but unresolved value raises
MISSING_PARAMETER. A conflicting read type raises PARAMETER_TYPE_MISMATCH.
The caller's fallback argument is not an approved default and cannot satisfy
these errors. Declared integer conversion still uses the preserved input token.

Public mutable custom numeric/string maps and the partial custom-value capture
helper have been removed. Model values are private resolved records. Get on
default-constructed storage raises INCOMPLETE_CONFIGURATION. Parameter-read
observers receive transient snapshots from those records and immutable raw input;
they cannot change scientific values. Numeric/boolean parser unit tests use the
narrow scalar functions; application reads use a named complete input fixture.

Network initialization now reads the resolved composition records through the
shared InitialComposition input reader. Default-constructed or modified
preparation state is rejected before fractions are read. Species keys are matched
case-insensitively by the declared composition contract; observations retain the
original input spelling, token and unit. Missing sparse members retain their
declared zero. Built-in and newly generated network adapters share only this
input step; their existing floor/normalization calculations are unchanged.
Previously generated external packages require regeneration before claiming
this input boundary. Full material-registration provenance remains separate.

### Material registration provenance

Application preparation now validates Host-side property provenance for every
registered species. ModelDefinition records the loaded case source identity;
ResolvedInput references its numeric configuration key; NetworkTable and
NetworkDefinition distinguish tabulated nuclear properties from adapter constants.
The record keeps the registered numeric value and rejects later mismatches.
An empty species registry remains valid for existing species-free models.

MaterialInput and MaterialConstant construct the input/model records from the
checked configuration. Numeric species views remain unchanged. Network owner
labels do not assert that an external package or complete build manifest has
been verified. The current JSON species snapshot still reports index/name only;
full material provenance presentation is not implemented by this change.

### Model-generated spatial composition

A case that constructs fractions in Init (currently Gaussian) declares that it
does not consume external composition. Its registered network species keys remain
known: explicit values retain their raw/parsed input and strict type/range/
duplicate validation, but applicability is not-applicable. Missing members have
null resolvedValue/valueSource, rather than synthetic zero fractions.
There is no positive-input-sum requirement for this declared path. Cases that
consume external fractions keep the positive finite sum requirement.
Inspection reports these unused explicit keys as unobserved, never as Init reads.
Network material registration uses SetupNetworkSpecies; the existing
SetupNetworkAndFractions path retains external composition resolution.

### Inspection coordinates with partial input

Version 3 inspection includes coordinates derived only from resolved geometry and
all three block counts. Missing/invalid topology (including active x3 with
inactive x2) returns null, never a default dimension. Valid topology can remain
available when unrelated physical inputs are incomplete. Names and units use the
same Core CoordinateMetadata mapping as preview; this is not a constructed mesh.
Host normalizes null to an absent optional coordinate object, not a fallback.

### Partial diffusion summary

Inspection returns diffusion only when EOS and use_diffusion are resolved;
otherwise it is null. The version 1 summary reuses preview's Core-owned channel,
transport-source and forbidden-explicit-key rules. Disabled is not unknown.
A forbidden coefficient remains in the input records with its diagnostic; the
summary does not remove or normalize it. State-dependent source is not an EOS
evaluation or proof of transport readiness. Host validates the summary before
exposing it and maps null to an absent optional object.

### Partial AMR selection summary

Inspection amrIndicators uses the same host-only selection parser and conditional
filtering as RuntimeParams. It requires resolved topology, use_burn and refine_var;
otherwise the summary is null. It does not construct a hierarchy or resolve case
species. With known dependencies, an alias or selection containing no usable
indicator returns INVALID_REFINEMENT_SELECTION and preserves the input token.
JENS availability is independent of selection: it is available only with self
gravity and an explicit CPU backend. Unsupported requests fail; no substitute
indicator is selected.

### O7 CPU Jeans target and checkpoint identity

The Core-owned catalog includes jeans_cells as a conditional dimensionless
float with no runtime default and an inclusive minimum of four. It is required
only when refine_var requests JENS; output-only selection does not require it.
A supplied out-of-range value remains invalid even when unused.

The frozen CPU uniform-lifecycle-1 short package is qualified. Public JENS
refinement and plot output require self gravity and explicit compute_backend=cpu;
auto and CUDA are not qualified and explicit requests fail without fallback.
Availability does not depend on whether JENS is already selected. An absent
refinement target stays null until the user explicitly provides jeans_cells.
This qualification does not advertise a JENS field in Initial Preview, nonlinear
JeansWave evolution, other EOS or complete RZ/AMR support.

Checkpoint state-controls revision 3 records active JENS refinement and its
consumed target. Output-only and unused target values do not change that
trajectory identity. Earlier controls revisions are rejected without migration;
raw checkpoints remain unchanged and require their original executable.
