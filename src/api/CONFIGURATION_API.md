# Studio 标准配置接口

配置扩展版本 `2`，外层 `schemaVersion="1.0"`。沿用 ARCH 的进程 + stdin/JSON 通道。无需运行 simulation、加载 EOS 表或启动 CUDA，也不写 `.par`、输出目录或数据文件。

## 两个入口

```sh
# 从当前可执行文件查询标准参数目录；不需要已有 .par。
build-studio-core-ui/bin/ARCH --config-schema

# 检查内存中的参数文本；这里用重定向演示，不要求 GUI 先保存。
build-studio-core-ui/bin/ARCH --inspect-config Sod --config-stdin \
  --request-id editor-001 < simulation/Sod/Sod.par
```

Host 直接调用可执行程序并写入 stdin。`--inspect-config` 使用与 Preview 相同的配置 1 MiB、响应 8 MiB、UTF-8/NUL 和标识符 128 字节限制；不接受采样参数。空 stdin 表示检查默认配置。退出码：0 成功；2 请求格式错误；3 配置错误；7 响应超限。stdout 为单个 JSON，底层日志不会混入它。

`--preview-capabilities.extensions.configuration` 发布入口和版本。现有 Preview、Core A 参数读取来源及 Sod 图形绑定保持独立。

## 参数目录：configuration-schema

`parameters` 恰含当前 `RuntimeParams` 的 90 个标准配置键，包括兼容别名。默认值来自 `src/core/config/StandardParameters.h`，实际 RuntimeParams 也使用这份定义。目录不会自动把默认值写入参数文件。

| 字段 | 含义 |
|---|---|
| key / type / group | 原始键、int/float/bool/string/expression、Grid/EOS/Network/Gravity/Diffusion/Runtime |
| defaultValue / defaultSource | 缺省输入；不是模型 Setup 或求解策略最终采用值。表达式默认输入可以是字符串 |
| constraints | 数值存储范围、语法和已确认的部分范围；`complete=false`，交叉约束及运行要求另检查 |
| options | 枚举选项、acceptedNames、大小写及未知值行为；策略列表从现有注册表生成 |
| aliasOf | `timeintegrator` 对应 `time_integrator`；同时提供时以后者为准 |
| applicability | 适用条件的解释；本次配置的布尔结果由检查接口提供 |
| path | 输入文件或输出目录、相对路径基准；普通字符串为 null |
| units | 单位及状态；坐标相关项通过 axis 指向坐标描述 |
| presentation | displayName、description、subgroup；90 键全覆盖。含指定参数的 toggle / enabledBy |

`presentation.toggle` 仅出现在 max_steps、plt_dt、plt_dstep、chk_dt、chk_dstep，启用条件 value > 0，关闭写入值 -1，未编辑原文保留。enabledBy 给出三个常量扩散系数对应的通道开关键。options.choices 的 displayName 用于显示，value 用于写回，acceptedNames 用于识别输入别名；不按别名逐个生成选项。

`options` 的 CPU/CUDA 标记仅描述注册的实现，不能用来认定当前 binary/device/依赖或组合已可运行。未知 solver 等选项原先会回退到默认策略，这一行为以 `unknownBehavior=core-fallback` 保留，不伪装成输入已经改写；严格报错选项使用 `error`。

`standardParametersComplete=true` 仅指上述标准键覆盖；`customParametersComplete=false`、`constraintsComplete=false`。结构体报告、缓存和未开放的字段不在目录中。自定义网络选项来自本次编译的注册表。

目录中 `ode_max_substeps=10000`、`ode_initial_dt_frac=0.001` 是实际参数解析缺省值；不要从结构体的 100 / 1e-14 初值代替它们。NSE 的输入是字符串 true/false/auto，不是单纯 bool。

## 配置检查：configuration-inspection

`identity` 包含传入的 caseId、requestId，以及原始输入字节的 configRevision SHA-256。Host 应继续给结果关联自己的项目和 binary/build 身份。caseId 在本接口是上下文标识，**不验证模型注册、不执行 Setup、不判断文件本来属于哪个模型**。

成功结果提供全部 90 个标准参数：

- `parsedValue`：类型转换后的配置输入。表达式返回求值后的数；这是 `typed-input-before-setup-and-policy-resolution`，不是完整 simulation 的最终有效值。
- `rawValue`：文件显式输入，否则 null；重复键仍以最后一项为准。
- `valueSource`：explicit、default 或 alias。`sourceKey` 指明别名来源，`defaultValue` 单独保留。
- `applicable`：依据当前模块开关等判断。范围明确为 configured-modules，不表示追踪到了模型实际使用它。即使不适用，文件显式提供的标准数值也要满足类型要求。
- `path` / `units`：路径用途及单位说明，不执行文件存在性检查。

`resolved` 提供维数、geometry、时间积分输入优先级等已解析摘要；`coordinates` 提供轴映射。`execution` 明确 Setup、EOS、CUDA、文件访问均未执行，simulationReadiness 未检查。

未识别标准键列入 `customParameters`，仅保留 rawValue，类型/单位为 null，状态为 `uninspected-model-parameter`。缺少 metadata 不表示参数未使用。

失败通过 `diagnostics` 返回 code、parameterKey、message、severity。完整数值错误和主要坐标/选项错误可定位字段；部分既有 RuntimeParams 交叉检查仍只给整体信息，此时 parameterKey 为 null。错误结果可能只包含已确认的部分参数，不要将其当作完整有效配置。

检查包括标准类型、表达式、现有 RuntimeParams 校验，以及轴 blocks/范围、AMR 层级和适用的注册选项；仍不替代模型 Setup、EOS 适用区间或完整求解器/设备验证。self gravity 会产生 unavailable 警告。Helmholtz + diffusion 显式传入 alpha_therm/nu_visc/D_spec 返回字段错误，不再从库内部直接退出进程。

## 坐标与单位

`coordinateSystems` 列出三种 geometry 在 1/2/3D 下的对应关系，来源为 `Grid::GetAxisNames()`。`coordinates.axes` 固定三项，包含：

- `key`：稳定的 x1/x2/x3。
- `displayName` / `nativeName`：易读名称及 Core 原始名称；r_cy/phi_cy 的显示名为 r/phi。
- `active`、kind、unit：活动状态、length/angle/inactive 等语义与单位。
- blocksKey、minKey、maxKey、lowerBoundaryKey、upperBoundaryKey：对应 `.par` 的原始键。

正整数 blocks（包括 1）启用该轴；第二/第三轴用 0 关闭，第三轴依赖第二轴。非活动轴显示名保留 x2/x3，其单位不猜测；GUI 可以使用目录中的目标维度模板提供启用提示。二维 cylindrical/spherical 为 r–phi，三维分别为 r–z–phi / r–theta–phi。

标准输入和输出统一采用 CGS，包括 IdealGas。单位字段不对用户数据做自动换算。

| 字段 | 单位 |
|---|---|
| 坐标长度 / 角度 | cm / rad |
| DENS | g/cm^3 |
| TEMP | K |
| PRES / ENER | erg/cm^3 |
| EINT | erg/g |
| VELX/VELY/VELZ | cm/s |

`state.units.system=cgs`、`basis=core-cgs-contract`、`valuesConverted=false`。兼容目录中的旧 code 字段仅为历史标签，当前运行不选择 code 单位。IdealGas 无组分回退比热修正为 7.18e6 erg/(g K)，显式 Cv 不自动换算；Sod 显式 Cv=1 的数值保持不变。

units.status 区分 known、dimensionless、not-applicable、coordinate-dependent、mixed-state 等。ode_atol 用于温度/丰度混合状态，没有一个统一标量单位；不显示为“单位不清楚”。未知 custom 单位仍为 null。Sod x_pos 通过已有明确 Cartesian 坐标绑定返回 cm 和 unitEvidence；没有通用 C++ 自动推断。

## Diffusion 与 AMR 展示状态

成功检查响应新增 `diffusion`：enabled、modeEditable=false、source、possibleSources、sourceScope、selectionRule、forbiddenExplicitKeys 和 channels。IdealGas 常量路径可按通道开关显示对应 cm^2/s 数值。Helmholtz 启用扩散时，三个常量键即使等于零也不得显式出现；普通关闭模块/通道则保留原值。物理系数最终取决于实际 EOS 状态，配置检查不会假装加载 EOS；stellar 分支目前只给出热扩散率。

`amrIndicators` 给出规范名字、Core 解析后 selected、当前 available 和不可用原因，以及需要 Setup 解析的组分名字。字段使用逗号分隔（兼容加号）；不能把未知文本自动当密度指标。Preview 状态也带该结构，实际网格和资源接口见 [INITIAL_AMR_API.md](INITIAL_AMR_API.md)。

## Host / Studio 接入边界

- 先读取目录构造编辑器；在需要检查当前副本时调用检查入口。失败时保留用户输入，旧结果按身份隔离。
- 默认项只有在用户编辑时才插入 `.par`；applicable=false 不意味着自动删除已有值。
- 路径的 relativeTo 是进程工作目录。Host 提供实际路径、文件类型、存在性、读写权限预检；Core 不调用文件系统完成此检查。
- EOS 输入表通常要在使用时存在；未启用的模块可以保留空路径。`eos_helm_table_path` 为空可选择 Core 已有的默认组件表，最终需要哪些表取决于加载策略。输出目录可以尚未创建。路径字段原始引号沿用实际加载器处理，Host 预检应使用相同规则。
- 使用配置身份和 Host 的 build/binary 身份淘汰旧响应；不要让另一模型的旧 metadata 驱动当前编辑器。
- 比例、颜色、上下限和截断是 Studio 的显示设置，不传回 `.par` 或 Core 数据数组。
- 配对警告、常驻名称、确认与覆盖保存由 Studio / Host 实现。本轮不增加声明文件或强制文件命名规则。

## 有意收紧的输入行为

标准 int 拒绝小数、科学计数文本、后缀和溢出；标准 float 拒绝非法后缀、非有限值和转换越界。坐标表达式只接受已有文档列出的形式，并拒绝不能得到有限结果的表达式。旧的数字前缀截断和表达式错误回退不再用于标准键。未填写的标准键默认值保持原值。

Custom 参数仍保留现有兼容读取，例如旧 Sod x_pos 数字前缀行为没有夹带改变。其严格化需随模型契约单独安排。接入方不能把标准检查标为“已验证全部 custom 参数”。

## 示例与验证

完整实际响应、输入和复现方式见 [examples/configuration](examples/configuration/README.md)。CPU 接口测试入口为 `configuration_api_contract`，旧六组 Preview 测试继续运行；交付结果及冻结基线见 [CORE_UI_HANDOFF.md](CORE_UI_HANDOFF.md)。
