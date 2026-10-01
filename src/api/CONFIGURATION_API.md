# Studio 配置接口 v3

当前集成分支的 Core 已实现配置扩展版本 3，外层仍为 schemaVersion="1.0"。
这是 O7.0 进行中的迁移，Host/Studio 传输与nullable表单已迁移；尚未完成动态摘要接线、受控只读运行配置和
全部旧输入迁移，不能作为阶段发布声明。v2 的 defaultValue/defaultSource、
缺项回填和 custom 未检查列表不再是本接口。

完整字段定义见 [v3 协议](CONFIGURATION_V3_CANDIDATE.md)；
必填／默认规则唯一来源仍是
[配置完整性计划](../../docs/development/ConfigurationContractPlan.zh-CN.md)。

## 入口与边界

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

parameters 是当前活动标准目录；本 binary 为 94 项，gravity_G 进入 retiredKeys。
25 个允许登记默认通过 allowedDefault 发布，其他项为 null。
这些数量仅是本次 binary 证据，客户端不得固定数量。

每项保留 key/type/group/presentation/options/path/units/constraints，
增加 usage、requirement、applicability、allowedDefault、templateRecommendations。
条件由 Core 发布 ID 和 dependencies，由 inspection 返回三态结果；
前端不能执行 description，也不能用缺少开关推断关闭。
推荐模板与允许默认分离；目前 templateRecommendations 为空。
constraints.complete=false，不代表全部模型物理规则已在 schema 枚举。

caseDeclarations 包含当前注册模型的来源路径、源码 SHA 和静态参数声明。
本 binary 包含 14 个模型；网络组分目录依赖选定 network，在 inspection 展开。
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

v3 inspection 不再通过带默认值的 SimConfig 构造 coordinates、diffusion、
amrIndicators 或旧 resolved 摘要。依赖未解析时必须保持未知。
Host/Studio已校验v3传输和显示nullable记录；相关动态展示仍需从共同解析结果接线，端到端迁移尚未完成；
不能从 v2 快照恢复虚假默认摘要。现有 Preview 的实际状态响应不因此成为
当前未完成配置的替代值。

## 客户端与验证

不兼容版本明确提示更新并保留 Working Copy，禁止 null 转 0/false 或旧默认。
草稿保存与运行资格独立；保存、inspection 都不能自动插入缺项或删除退役键。
未知／错误输入保持可定位，显式删除可 Undo。

当前真实二进制回归为 configuration_v3_contract；共同解析与实际入口
分别有 configuration_input、input_resolution、case_configuration、
config_input_records、configuration_entry_contract。不能把这些局部通过
称为全部 Core/Host/Studio 回归通过。旧 v2 fixtures 和调用方待迁移。
实际 v3 配置响应存于 examples/configuration-v3；原 examples/configuration
为历史 v2 证据，不应再作为新客户端协议期望。

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

Mutable custom numeric/string maps remain transitional adapters for existing
network/inspection consumers. Editing those maps cannot override Get's resolved
value or lexical identity, and preparation rejects adapter mutations. Narrow
unloaded lexical tests may still use the adapters, but cannot enter SetupChecked
or create a PreparedConfiguration. Removal of the remaining adapters is pending.

Network initialization now reads the resolved composition records through the
shared InitialComposition input reader. Default-constructed or modified
preparation state is rejected before fractions are read. Species keys are matched
case-insensitively by the declared composition contract; observations retain the
original input spelling, token and unit. Missing sparse members retain their
declared zero. Built-in and newly generated network adapters share only this
input step; their existing floor/normalization calculations are unchanged.
Previously generated external packages require regeneration before claiming
this input boundary. Full material-registration provenance remains separate.
